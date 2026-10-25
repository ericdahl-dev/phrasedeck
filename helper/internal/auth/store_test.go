package auth

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeTokens(t *testing.T, mode os.FileMode, tokens ...string) string {
	t.Helper()
	var devs []string
	for i, tok := range tokens {
		sum := sha256.Sum256([]byte(tok))
		devs = append(devs, fmt.Sprintf(`{"name":"dev%d","sha256":%q,"paired_at":"2026-10-24T00:00:00Z"}`, i, hex.EncodeToString(sum[:])))
	}
	p := filepath.Join(t.TempDir(), "tokens.json")
	if err := os.WriteFile(p, []byte(`{"devices":[`+strings.Join(devs, ",")+`]}`), mode); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(p, mode); err != nil { // WriteFile's mode is masked by umask
		t.Fatal(err)
	}
	return p
}

func TestPresetTokenChecks(t *testing.T) {
	s, err := NewStore(writeTokens(t, 0o600, "other", "secret"))
	if err != nil {
		t.Fatal(err)
	}
	if !s.Check("secret") {
		t.Fatal("preset token rejected")
	}
	if s.Check("wrong") {
		t.Fatal("wrong token accepted")
	}
}

func TestMissingFileIsEmpty(t *testing.T) {
	s, err := NewStore(filepath.Join(t.TempDir(), "tokens.json"))
	if err != nil {
		t.Fatal(err)
	}
	if s.Check("anything") {
		t.Fatal("empty store accepted a token")
	}
}

func TestLooseModeRefused(t *testing.T) {
	_, err := NewStore(writeTokens(t, 0o644, "secret"))
	if err == nil || !strings.Contains(err.Error(), "chmod 600") {
		t.Fatalf("want a chmod 600 error, got %v", err)
	}
}

func TestEmptyTokenNeverChecks(t *testing.T) {
	s, err := NewStore(writeTokens(t, 0o600, ""))
	if err != nil {
		t.Fatal(err)
	}
	if s.Check("") {
		t.Fatal("empty token accepted")
	}
}

func TestBadJSONRefused(t *testing.T) {
	p := filepath.Join(t.TempDir(), "tokens.json")
	if err := os.WriteFile(p, []byte("{"), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := NewStore(p); err == nil {
		t.Fatal("bad JSON loaded")
	}
}
