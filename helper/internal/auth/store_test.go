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

func TestAddChecksAndStoresOnlyTheHash(t *testing.T) {
	p := filepath.Join(t.TempDir(), "phrased", "tokens.json")
	s, err := NewStore(p)
	if err != nil {
		t.Fatal(err)
	}
	tok, err := s.Add("iPhone")
	if err != nil {
		t.Fatal(err)
	}
	if len(tok) != 64 || !s.Check(tok) {
		t.Fatalf("token %q doesn't check", tok)
	}
	data, err := os.ReadFile(p)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(data), tok) {
		t.Fatal("plain token written to disk")
	}
	info, _ := os.Stat(p)
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("token file mode %v, want 0600", info.Mode().Perm())
	}
	again, err := NewStore(p)
	if err != nil || !again.Check(tok) {
		t.Fatalf("token lost on reload: %v", err)
	}
}

func TestAddDuplicateNameGetsSuffix(t *testing.T) {
	s, _ := NewStore(filepath.Join(t.TempDir(), "tokens.json"))
	for i := 0; i < 3; i++ {
		if _, err := s.Add("iPhone"); err != nil {
			t.Fatal(err)
		}
	}
	var names []string
	for _, d := range s.Devices() {
		names = append(names, d.Name)
	}
	if strings.Join(names, ",") != "iPhone,iPhone (2),iPhone (3)" {
		t.Fatalf("names %v", names)
	}
}

func TestRevokeFromAnotherStoreIsSeenWithoutRestart(t *testing.T) {
	p := filepath.Join(t.TempDir(), "tokens.json")
	server, _ := NewStore(p)
	keep, _ := server.Add("iPad")
	tok, _ := server.Add("iPhone")

	cli, err := NewStore(p)
	if err != nil {
		t.Fatal(err)
	}
	if err := cli.Remove("iPhone"); err != nil {
		t.Fatal(err)
	}
	if server.Check(tok) {
		t.Fatal("revoked token still checks in the running store")
	}
	if !server.Check(keep) {
		t.Fatal("other device lost")
	}
	if err := cli.Remove("iPhone"); err == nil {
		t.Fatal("removing an unknown device should fail")
	}
}

func TestAddFromAnotherStoreIsSeen(t *testing.T) {
	p := filepath.Join(t.TempDir(), "tokens.json")
	server, _ := NewStore(p)
	other, _ := NewStore(p)
	tok, _ := other.Add("curl")
	if !server.Check(tok) {
		t.Fatal("token added by another process not picked up")
	}
}

func TestReloadOfBrokenFileFailsClosed(t *testing.T) {
	p := filepath.Join(t.TempDir(), "tokens.json")
	s, _ := NewStore(p)
	tok, _ := s.Add("iPhone")
	if err := os.WriteFile(p, []byte("{ broken and longer"), 0o600); err != nil {
		t.Fatal(err)
	}
	if s.Check(tok) {
		t.Fatal("broken token file still accepted the old token")
	}
}
