// Package auth checks bearer tokens against the hashes in the token file.
package auth

import (
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"sync"
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

// Store holds the token hashes from one file.
type Store struct {
	mu     sync.Mutex
	path   string
	hashes [][]byte
}

// NewStore loads path, or starts empty if it doesn't exist. A file readable by group or others
// is refused.
func NewStore(path string) (*Store, error) {
	s := &Store{path: path}
	info, err := os.Stat(path)
	if errors.Is(err, fs.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return nil, err
	}
	if info.Mode().Perm()&0o077 != 0 {
		return nil, fmt.Errorf("%s is mode %v; run chmod 600 %s", path, info.Mode().Perm(), path)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var f file
	if err := json.Unmarshal(data, &f); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	for _, d := range f.Devices {
		h, err := hex.DecodeString(d.SHA256)
		if err != nil || len(h) != sha256.Size {
			return nil, fmt.Errorf("%s: device %q has a bad sha256", path, d.Name)
		}
		s.hashes = append(s.hashes, h)
	}
	return s, nil
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
	ok := 0
	for _, h := range s.hashes {
		ok |= subtle.ConstantTimeCompare(sum[:], h)
	}
	return ok == 1
}
