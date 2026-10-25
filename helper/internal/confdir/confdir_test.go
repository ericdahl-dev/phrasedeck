package confdir

import (
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"testing"
	"time"
)

func TestWhatTheHelperServesIsWhatPairReads(t *testing.T) {
	d := New(t.TempDir())
	if port, ip, lanPort := d.Advertised(); port != 0 || ip != "" || lanPort != 0 {
		t.Fatalf("empty dir advertises %d %q %d", port, ip, lanPort)
	}
	if err := d.RecordPort(47900); err != nil {
		t.Fatal(err)
	}
	if err := d.RecordLAN("fd00::5", 47912); err != nil {
		t.Fatal(err)
	}
	if port, ip, lanPort := d.Advertised(); port != 47900 || ip != "fd00::5" || lanPort != 47912 {
		t.Fatalf("advertised %d %q %d", port, ip, lanPort)
	}
	// LAN off (or no LAN address): a stale route must never reach the QR code.
	if err := d.RecordLAN("", 0); err != nil {
		t.Fatal(err)
	}
	if _, ip, lanPort := d.Advertised(); ip != "" || lanPort != 0 {
		t.Fatalf("LAN still advertised: %q %d", ip, lanPort)
	}
	if err := d.RecordLAN("", 0); err != nil {
		t.Fatalf("clearing twice: %v", err)
	}
}

// Config dirs written by earlier helpers keep working: same names, same formats.
func TestExistingConfigDirsKeepWorking(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "port"), []byte("47900\n"), 0o600)
	os.WriteFile(filepath.Join(dir, "lan-addr"), []byte("192.168.1.20:47812\n"), 0o600)
	os.WriteFile(filepath.Join(dir, "lan-subnets"), []byte("# home\n192.168.1.0/24\n\n10.0.0.0/8 # lab\n"), 0o600)
	d := New(dir)
	if port, ip, lanPort := d.Advertised(); port != 47900 || ip != "192.168.1.20" || lanPort != 47812 {
		t.Fatalf("advertised %d %q %d", port, ip, lanPort)
	}
	got, err := d.Subnets()
	if err != nil || !reflect.DeepEqual(got, []string{"192.168.1.0/24", "10.0.0.0/8"}) {
		t.Fatalf("subnets %v, %v", got, err)
	}
}

func TestGarbageRecordsReadAsNotRecorded(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "port"), []byte("nope\n"), 0o600)
	os.WriteFile(filepath.Join(dir, "lan-addr"), []byte("192.168.1.20\n"), 0o600)
	if port, ip, lanPort := New(dir).Advertised(); port != 0 || ip != "" || lanPort != 0 {
		t.Fatalf("advertised %d %q %d", port, ip, lanPort)
	}
}

func TestNoSubnetsFileMeansNoSubnets(t *testing.T) {
	if got, err := New(t.TempDir()).Subnets(); got != nil || err != nil {
		t.Fatalf("got %v, %v", got, err)
	}
}

func TestRecordsAreReadableOnlyByTheUser(t *testing.T) {
	d := New(filepath.Join(t.TempDir(), "phrased"))
	d.RecordPort(47811)
	d.RecordLAN("192.168.1.20", 47812)
	if _, err := d.OpenPairingWindow(time.Now()); err != nil {
		t.Fatal(err)
	}
	if _, _, err := d.Cert(); err != nil {
		t.Fatal(err)
	}
	entries, _ := os.ReadDir(d.path)
	if len(entries) != 5 {
		t.Fatalf("files %v", entries)
	}
	for _, e := range entries {
		info, _ := e.Info()
		if info.Mode().Perm() != 0o600 {
			t.Errorf("%s has mode %v, want 0600", e.Name(), info.Mode().Perm())
		}
	}
	info, _ := os.Stat(d.path)
	if info.Mode().Perm() != 0o700 {
		t.Errorf("dir mode %v, want 0700", info.Mode().Perm())
	}
}

func TestPairingWindowOpensForTheStoreAndPairer(t *testing.T) {
	d := New(t.TempDir())
	code, err := d.OpenPairingWindow(time.Now())
	if err != nil || !regexp.MustCompile(`^\d{6}$`).MatchString(code) {
		t.Fatalf("code %q, %v", code, err)
	}
	store, err := d.Store()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := d.Pairer(store).Pair(code, "iPhone"); err != nil {
		t.Fatalf("pairing with the window's code: %v", err)
	}
	if len(store.Devices()) != 1 {
		t.Fatalf("devices %v", store.Devices())
	}
}

func TestTheCertIsMadeOnceAndItsFingerprintIsReadable(t *testing.T) {
	d := New(t.TempDir())
	if _, err := d.Fingerprint(); err == nil {
		t.Fatal("fingerprint before any cert")
	}
	_, fp, err := d.Cert()
	if err != nil {
		t.Fatal(err)
	}
	_, again, err := d.Cert()
	if err != nil || again != fp {
		t.Fatalf("second load %q, %v; want %q", again, err, fp)
	}
	if got, err := d.Fingerprint(); err != nil || got != fp {
		t.Fatalf("Fingerprint %q, %v; want %q", got, err, fp)
	}
}
