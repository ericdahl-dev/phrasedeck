package main

import (
	"bytes"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/auth"
)

func runCmd(t *testing.T, args ...string) (string, error) {
	t.Helper()
	var out bytes.Buffer
	handled, err := command(args, "/unused-home", &out)
	if !handled {
		t.Fatalf("%v not handled as a command", args)
	}
	return out.String(), err
}

func TestPairCommandOpensWindow(t *testing.T) {
	dir := t.TempDir()
	var notified string
	notify = func(code string) { notified = code }
	t.Cleanup(func() { notify = macNotify })

	out, err := runCmd(t, "pair", "-config", dir)
	if err != nil {
		t.Fatal(err)
	}
	code := regexp.MustCompile(`\b\d{6}\b`).FindString(out)
	if code == "" {
		t.Fatalf("no 6-digit code in %q", out)
	}
	if notified != code {
		t.Fatalf("notified %q, printed %q", notified, code)
	}
	store, _ := auth.NewStore(filepath.Join(dir, "tokens.json"))
	if _, err := auth.NewPairer(filepath.Join(dir, "pairing.json"), store).Pair(code, "iPhone"); err != nil {
		t.Fatalf("printed code doesn't pair: %v", err)
	}
}

func TestDevicesAndRevoke(t *testing.T) {
	dir := t.TempDir()
	store, _ := auth.NewStore(filepath.Join(dir, "tokens.json"))
	store.Add("iPhone")
	store.Add("iPad")

	out, err := runCmd(t, "devices", "-config", dir)
	if err != nil || !strings.Contains(out, "iPhone") || !strings.Contains(out, "iPad") {
		t.Fatalf("devices: %q %v", out, err)
	}
	if _, err := runCmd(t, "revoke", "-config", dir, "iPhone"); err != nil {
		t.Fatal(err)
	}
	out, _ = runCmd(t, "devices", "-config", dir)
	if strings.Contains(out, "iPhone") {
		t.Fatalf("iPhone still listed: %q", out)
	}
	if _, err := runCmd(t, "revoke", "-config", dir, "iPhone"); err == nil {
		t.Fatal("revoking an unknown device should fail")
	}
	if _, err := runCmd(t, "revoke", "-config", dir); err == nil {
		t.Fatal("revoke without a name should fail")
	}
}

func TestDevicesEmpty(t *testing.T) {
	out, err := runCmd(t, "devices", "-config", t.TempDir())
	if err != nil || !strings.Contains(out, "no paired devices") {
		t.Fatalf("got %q %v", out, err)
	}
}

func TestServerFlagsAreNotACommand(t *testing.T) {
	handled, _ := command([]string{"-port", "1"}, "/h", os.Stdout)
	if handled {
		t.Fatal("flags treated as a command")
	}
}
