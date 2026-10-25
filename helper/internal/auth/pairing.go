package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"math/big"
	"os"
	"sync"
	"time"
)

const (
	// PairWindow is how long a pairing code from `phrased pair` stays valid.
	PairWindow = 2 * time.Minute
	// MaxFailures wrong codes lock pairing until a new window opens.
	MaxFailures = 10
)

var (
	// ErrNoWindow means no pairing window is open: none was opened, it expired, it was used,
	// or pairing is locked. The server answers 404.
	ErrNoWindow = errors.New("no pairing window open; run phrased pair on this computer")
	// ErrWrongCode is a wrong pairing code. The server answers 401.
	ErrWrongCode = errors.New("wrong pairing code")
)

// window is pairing.json. It holds the code's hash, never the code.
type window struct {
	ID      string    `json:"window"`
	CodeSHA string    `json:"code_sha256"`
	Expires time.Time `json:"expires"`
}

func codeHash(windowID, code string) []byte {
	sum := sha256.Sum256([]byte(windowID + ":" + code))
	return sum[:]
}

// OpenWindow writes a new pairing window to path, valid for PairWindow from now, and returns
// its 6-digit code.
func OpenWindow(path string, now time.Time) (string, error) {
	n, err := rand.Int(rand.Reader, big.NewInt(1_000_000))
	if err != nil {
		return "", err
	}
	code := fmt.Sprintf("%06d", n.Int64())
	id := make([]byte, 16)
	if _, err := rand.Read(id); err != nil {
		return "", err
	}
	w := window{ID: hex.EncodeToString(id), Expires: now.Add(PairWindow).UTC()}
	w.CodeSHA = hex.EncodeToString(codeHash(w.ID, code))
	data, err := json.Marshal(w)
	if err != nil {
		return "", err
	}
	return code, WritePrivate(path, data)
}

// Pairer exchanges a code from an open window for a device token. It runs in the helper and
// reads pairing.json on every attempt, so it sees windows opened by `phrased pair`.
type Pairer struct {
	Now func() time.Time

	mu       sync.Mutex
	path     string
	store    *Store
	windowID string // the window the failure count belongs to
	failures int
	used     string // the last window that paired, in case deleting its file failed
}

func NewPairer(path string, store *Store) *Pairer {
	return &Pairer{Now: time.Now, path: path, store: store}
}

// Pair checks code against the open window and, if it matches, pairs device and returns its
// token. The window closes on success.
func (p *Pairer) Pair(code, device string) (string, error) {
	p.mu.Lock()
	defer p.mu.Unlock()
	data, err := os.ReadFile(p.path)
	if err != nil {
		return "", ErrNoWindow
	}
	var w window
	if err := json.Unmarshal(data, &w); err != nil || w.ID == "" || w.ID == p.used || !p.Now().Before(w.Expires) {
		return "", ErrNoWindow
	}
	if w.ID != p.windowID {
		p.windowID, p.failures = w.ID, 0
	}
	if p.failures >= MaxFailures {
		return "", ErrNoWindow
	}
	want, err := hex.DecodeString(w.CodeSHA)
	if err != nil || subtle.ConstantTimeCompare(codeHash(w.ID, code), want) != 1 {
		p.failures++
		return "", ErrWrongCode
	}
	token, err := p.store.Add(device)
	if err != nil {
		return "", err
	}
	p.used = w.ID
	os.Remove(p.path)
	return token, nil
}
