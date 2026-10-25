package auth

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"
)

type pairEnv struct {
	t     *testing.T
	dir   string
	now   time.Time
	store *Store
	p     *Pairer
}

func newPairEnv(t *testing.T) *pairEnv {
	t.Helper()
	dir := t.TempDir()
	store, err := NewStore(filepath.Join(dir, "tokens.json"))
	if err != nil {
		t.Fatal(err)
	}
	e := &pairEnv{t: t, dir: dir, now: time.Date(2026, 10, 24, 12, 0, 0, 0, time.UTC), store: store}
	e.p = NewPairer(filepath.Join(dir, "pairing.json"), store)
	e.p.Now = func() time.Time { return e.now }
	return e
}

func (e *pairEnv) open() string {
	e.t.Helper()
	code, err := OpenWindow(filepath.Join(e.dir, "pairing.json"), e.now)
	if err != nil {
		e.t.Fatal(err)
	}
	return code
}

func wrongCode(code string) string {
	if code == "000000" {
		return "000001"
	}
	return "000000"
}

func TestOpenWindowCode(t *testing.T) {
	e := newPairEnv(t)
	code := e.open()
	if !regexp.MustCompile(`^\d{6}$`).MatchString(code) {
		t.Fatalf("code %q isn't 6 digits", code)
	}
	data, err := os.ReadFile(filepath.Join(e.dir, "pairing.json"))
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(data), code) {
		t.Fatal("plain code written to pairing.json")
	}
	info, _ := os.Stat(filepath.Join(e.dir, "pairing.json"))
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("pairing.json mode %v, want 0600", info.Mode().Perm())
	}
}

func TestPairNoWindow(t *testing.T) {
	e := newPairEnv(t)
	if _, err := e.p.Pair("123456", "iPhone"); err != ErrNoWindow {
		t.Fatalf("want ErrNoWindow, got %v", err)
	}
}

func TestPairWrongThenRightThenReuse(t *testing.T) {
	e := newPairEnv(t)
	code := e.open()
	if _, err := e.p.Pair(wrongCode(code), "iPhone"); err != ErrWrongCode {
		t.Fatalf("want ErrWrongCode, got %v", err)
	}
	tok, err := e.p.Pair(code, "iPhone")
	if err != nil {
		t.Fatal(err)
	}
	if !e.store.Check(tok) {
		t.Fatal("paired token doesn't check")
	}
	if _, err := e.p.Pair(code, "iPhone"); err != ErrNoWindow {
		t.Fatalf("code reused: want ErrNoWindow, got %v", err)
	}
	if _, err := os.Stat(filepath.Join(e.dir, "pairing.json")); !os.IsNotExist(err) {
		t.Fatal("pairing.json left after a successful pair")
	}
	data, _ := os.ReadFile(filepath.Join(e.dir, "tokens.json"))
	if strings.Contains(string(data), tok) || strings.Contains(string(data), code) {
		t.Fatal("plain token or code written to tokens.json")
	}
}

func TestPairExpired(t *testing.T) {
	e := newPairEnv(t)
	code := e.open()
	e.now = e.now.Add(PairWindow + time.Second)
	if _, err := e.p.Pair(code, "iPhone"); err != ErrNoWindow {
		t.Fatalf("want ErrNoWindow after expiry, got %v", err)
	}
}

func TestPairLockoutAndReset(t *testing.T) {
	e := newPairEnv(t)
	code := e.open()
	for i := 0; i < MaxFailures; i++ {
		if _, err := e.p.Pair(wrongCode(code), "iPhone"); err != ErrWrongCode {
			t.Fatalf("attempt %d: want ErrWrongCode, got %v", i+1, err)
		}
	}
	if _, err := e.p.Pair(code, "iPhone"); err != ErrNoWindow {
		t.Fatalf("locked: want ErrNoWindow, got %v", err)
	}
	code = e.open() // a new window resets the count
	if _, err := e.p.Pair(code, "iPhone"); err != nil {
		t.Fatalf("new window: %v", err)
	}
}

func TestPairBadCodeFormatCountsAsWrong(t *testing.T) {
	e := newPairEnv(t)
	e.open()
	if _, err := e.p.Pair("12345", "iPhone"); err != ErrWrongCode {
		t.Fatalf("want ErrWrongCode, got %v", err)
	}
}
