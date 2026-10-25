package lan

import (
	"crypto/ecdsa"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

func TestCertIsMadeOnceAndReused(t *testing.T) {
	dir := t.TempDir()
	first, fp, err := LoadOrCreateCert(dir)
	if err != nil {
		t.Fatal(err)
	}
	again, fp2, err := LoadOrCreateCert(dir)
	if err != nil {
		t.Fatal(err)
	}
	if fp != fp2 || string(first.Certificate[0]) != string(again.Certificate[0]) {
		t.Fatal("a second start made a new certificate")
	}
	if _, ok := again.PrivateKey.(*ecdsa.PrivateKey); !ok {
		t.Fatalf("key is %T, want ECDSA", again.PrivateKey)
	}
}

func TestFingerprintIsLowercaseHexSHA256OfTheLeaf(t *testing.T) {
	cert, fp, err := LoadOrCreateCert(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256(cert.Certificate[0])
	if fp != hex.EncodeToString(sum[:]) || !regexp.MustCompile(`^[0-9a-f]{64}$`).MatchString(fp) {
		t.Fatalf("fingerprint %q", fp)
	}
	if got, err := Fingerprint(t.TempDir()); err == nil || got != "" {
		t.Fatalf("Fingerprint of an empty dir = %q, %v", got, err)
	}
}

func TestKeyIsPrivateToTheUser(t *testing.T) {
	dir := t.TempDir()
	if _, _, err := LoadOrCreateCert(dir); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(filepath.Join(dir, keyFile))
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("key mode %v, want 0600", info.Mode().Perm())
	}
}

func TestAHalfWrittenPairIsReplaced(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, certFile), []byte("junk"), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, _, err := LoadOrCreateCert(dir); err != nil {
		t.Fatalf("a cert without its key should be replaced, got %v", err)
	}
}

// A pinned certificate must never change silently: a key that exists but can't be read is an
// error naming the file, and the files stay as they are.
func TestAnUnreadableKeyIsAnErrorNotANewCert(t *testing.T) {
	for name, spoil := range map[string]func(path string) error{
		"corrupt": func(p string) error { return os.WriteFile(p, []byte("junk"), 0o600) },
		"no permission": func(p string) error {
			if os.Getuid() == 0 {
				t.Skip("root reads any file")
			}
			return os.Chmod(p, 0)
		},
	} {
		t.Run(name, func(t *testing.T) {
			dir := t.TempDir()
			if _, _, err := LoadOrCreateCert(dir); err != nil {
				t.Fatal(err)
			}
			certBefore, _ := os.ReadFile(filepath.Join(dir, certFile))
			key := filepath.Join(dir, keyFile)
			if err := spoil(key); err != nil {
				t.Fatal(err)
			}
			_, _, err := LoadOrCreateCert(dir)
			if err == nil || !strings.Contains(err.Error(), keyFile) {
				t.Fatalf("err = %v, want one naming %s", err, keyFile)
			}
			if after, _ := os.ReadFile(filepath.Join(dir, certFile)); string(after) != string(certBefore) {
				t.Fatal("the certificate was replaced")
			}
		})
	}
}
