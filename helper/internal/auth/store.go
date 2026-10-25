// Package auth checks bearer tokens against the hashes in the token file, and runs pairing.
package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// Device is one paired device. Only the SHA-256 of its token is stored.
type Device struct {
	Name     string `json:"name"`
	SHA256   string `json:"sha256"`
	PairedAt string `json:"paired_at"`
}

type file struct {
	Devices []Device `json:"devices"`
}

// stamp identifies one version of the token file.
type stamp struct {
	exists bool
	mod    time.Time
	size   int64
}

// Store holds the devices from one token file. It reloads the file when it changes, so a
// device added or revoked by another process counts on the next call.
type Store struct {
	mu      sync.Mutex
	path    string
	devices []Device
	hashes  [][]byte
	seen    stamp
	err     error // the last load failed; Check fails closed until the file is fixed
}

// NewStore loads path, or starts empty if it doesn't exist. A file readable by group or others
// is refused.
func NewStore(path string) (*Store, error) {
	s := &Store{path: path}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.load()
	return s, s.err
}

func (s *Store) stat() (stamp, os.FileInfo, error) {
	info, err := os.Stat(s.path)
	if errors.Is(err, fs.ErrNotExist) {
		return stamp{}, nil, nil
	}
	if err != nil {
		return stamp{}, nil, err
	}
	return stamp{exists: true, mod: info.ModTime(), size: info.Size()}, info, nil
}

// load reads the file into s. The caller holds s.mu.
func (s *Store) load() {
	st, info, err := s.stat()
	s.seen, s.devices, s.hashes, s.err = st, nil, nil, err
	if err != nil || !st.exists {
		return
	}
	if info.Mode().Perm()&0o077 != 0 {
		s.err = fmt.Errorf("%s is mode %v; run chmod 600 %s", s.path, info.Mode().Perm(), s.path)
		return
	}
	data, err := os.ReadFile(s.path)
	if err != nil {
		s.err = err
		return
	}
	var f file
	if err := json.Unmarshal(data, &f); err != nil {
		s.err = fmt.Errorf("%s: %w", s.path, err)
		return
	}
	hashes := make([][]byte, 0, len(f.Devices))
	for _, d := range f.Devices {
		h, err := hex.DecodeString(d.SHA256)
		if err != nil || len(h) != sha256.Size {
			s.err = fmt.Errorf("%s: device %q has a bad sha256", s.path, d.Name)
			return
		}
		hashes = append(hashes, h)
	}
	s.devices, s.hashes = f.Devices, hashes
}

// refresh reloads the file if it changed since the last load. The caller holds s.mu.
func (s *Store) refresh() {
	st, _, err := s.stat()
	if err != nil || st != s.seen {
		s.load()
	}
}

// Check reports whether token matches a stored hash. It compares against every hash in
// constant time and never returns early.
func (s *Store) Check(token string) bool {
	if token == "" {
		return false
	}
	sum := sha256.Sum256([]byte(token))
	s.mu.Lock()
	defer s.mu.Unlock()
	s.refresh()
	if s.err != nil {
		return false
	}
	ok := 0
	for _, h := range s.hashes {
		ok |= subtle.ConstantTimeCompare(sum[:], h)
	}
	return ok == 1
}

// Devices returns the paired devices.
func (s *Store) Devices() []Device {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.refresh()
	return append([]Device(nil), s.devices...)
}

// Add pairs a device and returns its new token. Only the token's hash is saved. A name already
// in use gets a suffix: "iPhone (2)" and so on.
func (s *Store) Add(name string) (string, error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	token := hex.EncodeToString(raw)
	sum := sha256.Sum256([]byte(token))

	s.mu.Lock()
	defer s.mu.Unlock()
	s.refresh()
	if s.err != nil {
		return "", s.err
	}
	unique := name
	for n := 2; s.has(unique); n++ {
		unique = fmt.Sprintf("%s (%d)", name, n)
	}
	devices := append(append([]Device(nil), s.devices...), Device{
		Name:     unique,
		SHA256:   hex.EncodeToString(sum[:]),
		PairedAt: time.Now().UTC().Format(time.RFC3339),
	})
	if err := s.save(devices); err != nil {
		return "", err
	}
	return token, nil
}

// Remove revokes the device with this name.
func (s *Store) Remove(name string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.refresh()
	if s.err != nil {
		return s.err
	}
	var devices []Device
	for _, d := range s.devices {
		if d.Name != name {
			devices = append(devices, d)
		}
	}
	if len(devices) == len(s.devices) {
		return fmt.Errorf("no device named %q", name)
	}
	return s.save(devices)
}

func (s *Store) has(name string) bool {
	for _, d := range s.devices {
		if d.Name == name {
			return true
		}
	}
	return false
}

// save writes devices and reloads them. The caller holds s.mu.
// shortcut: two processes editing at the same moment can lose one edit; fine for one person
// running phrased pair or revoke by hand.
func (s *Store) save(devices []Device) error {
	if devices == nil {
		devices = []Device{}
	}
	data, err := json.MarshalIndent(file{Devices: devices}, "", "  ")
	if err != nil {
		return err
	}
	if err := writePrivate(s.path, append(data, '\n')); err != nil {
		return err
	}
	s.load()
	return s.err
}

// writePrivate writes data to path with mode 0600, through a temp file and a rename, creating
// the directory with mode 0700 if needed.
func writePrivate(path string, data []byte) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(dir, ".tmp-*")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	if err := tmp.Chmod(0o600); err != nil {
		tmp.Close()
		return err
	}
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	return os.Rename(tmp.Name(), path)
}
