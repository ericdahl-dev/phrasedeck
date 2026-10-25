package main

import (
	"bytes"
	"errors"
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
	tailscaleStatus = func(string) ([]byte, error) { return nil, errors.New("not running") }
	t.Cleanup(func() { notify = macNotify; tailscaleStatus = runTailscaleStatus })

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

func TestPairURLEscapesItsFields(t *testing.T) {
	got := pairURL("lib-2833.tail1234.ts.net", 47811, "012345")
	want := "phrasedeck://pair?code=012345&host=lib-2833.tail1234.ts.net&port=47811&v=1"
	if got != want {
		t.Fatalf("pairURL = %q, want %q", got, want)
	}
	if got := pairURL("a&b=c", 1, "1"); !strings.Contains(got, "host=a%26b%3Dc") {
		t.Fatalf("host not escaped: %q", got)
	}
}

func TestPairCommandPrintsAQRCodeForTheTailnetName(t *testing.T) {
	dir := t.TempDir()
	notify = func(string) {}
	tailscaleStatus = func(string) ([]byte, error) {
		return []byte(`{"Self":{"HostName":"mac","DNSName":"mac.tail1234.ts.net.","TailscaleIPs":["100.69.235.48"]}}`), nil
	}
	t.Cleanup(func() { notify = macNotify; tailscaleStatus = runTailscaleStatus })
	os.WriteFile(filepath.Join(dir, "port"), []byte("47900\n"), 0o600)

	out, err := runCmd(t, "pair", "-config", dir)
	if err != nil {
		t.Fatal(err)
	}
	code := regexp.MustCompile(`\b\d{6}\b`).FindString(out)
	if !strings.Contains(out, pairURL("mac.tail1234.ts.net", 47900, code)) {
		t.Fatalf("no pair URL with the running port in %q", out)
	}
	if !strings.Contains(out, "█") || strings.Index(out, "█") < strings.Index(out, code) {
		t.Fatalf("no QR code under the text code in %q", out)
	}
}

func TestPairCommandStillPrintsTheCodeWithoutTailscale(t *testing.T) {
	dir := t.TempDir()
	notify = func(string) {}
	tailscaleStatus = func(string) ([]byte, error) { return nil, errors.New("not running") }
	t.Cleanup(func() { notify = macNotify; tailscaleStatus = runTailscaleStatus })

	out, err := runCmd(t, "pair", "-config", dir)
	if err != nil {
		t.Fatal(err)
	}
	if regexp.MustCompile(`\b\d{6}\b`).FindString(out) == "" || !strings.Contains(out, "No QR code") {
		t.Fatalf("want the code and a no-QR note, got %q", out)
	}
}
