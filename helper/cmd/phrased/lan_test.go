package main

import (
	"errors"
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"strings"
	"testing"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/lan"
)

const fp64 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

func TestPairURLAddsFpAndLanTogether(t *testing.T) {
	got := pairURL("mac.tail1234.ts.net", 47811, "012345", fp64, "192.168.1.20")
	want := "phrasedeck://pair?code=012345&fp=" + fp64 + "&host=mac.tail1234.ts.net&lan=192.168.1.20&port=47811&v=1"
	if got != want {
		t.Fatalf("pairURL = %q, want %q", got, want)
	}
	for _, half := range [][2]string{{fp64, ""}, {"", "192.168.1.20"}} {
		if got := pairURL("h", 1, "1", half[0], half[1]); strings.Contains(got, "fp=") || strings.Contains(got, "lan=") {
			t.Errorf("fp or lan without the other: %q", got)
		}
	}
}

// lanConfig is a config dir as a helper running with -lan leaves it.
func lanConfig(t *testing.T) (dir, fp string) {
	t.Helper()
	dir = t.TempDir()
	_, fp, err := lan.LoadOrCreateCert(dir)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, lanAddrFile), []byte("192.168.1.20\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	return dir, fp
}

func TestPairQRCarriesTheLANPinWhenLANIsOn(t *testing.T) {
	dir, fp := lanConfig(t)
	notify = func(string) {}
	tailscaleStatus = func(string) ([]byte, error) {
		return []byte(`{"Self":{"HostName":"mac","DNSName":"mac.tail1234.ts.net.","TailscaleIPs":["100.69.235.48"]}}`), nil
	}
	t.Cleanup(func() { notify = macNotify; tailscaleStatus = runTailscaleStatus })

	out, err := runCmd(t, "pair", "-config", dir)
	if err != nil {
		t.Fatal(err)
	}
	code := regexp.MustCompile(`\b\d{6}\b`).FindString(out)
	if !strings.Contains(out, pairURL("mac.tail1234.ts.net", 47811, code, fp, "192.168.1.20")) {
		t.Fatalf("no pair URL with fp and lan in %q", out)
	}
}

func TestPairQRWithTailscaleOffUsesTheLANAddress(t *testing.T) {
	dir, fp := lanConfig(t)
	notify = func(string) {}
	tailscaleStatus = func(string) ([]byte, error) { return nil, errors.New("not running") }
	t.Cleanup(func() { notify = macNotify; tailscaleStatus = runTailscaleStatus })

	out, err := runCmd(t, "pair", "-config", dir)
	if err != nil {
		t.Fatal(err)
	}
	code := regexp.MustCompile(`\b\d{6}\b`).FindString(out)
	if !strings.Contains(out, pairURL("192.168.1.20", 47811, code, fp, "192.168.1.20")) {
		t.Fatalf("want a LAN-only pair URL in %q", out)
	}
}

func TestSubnetsComeFromTheFileAndFlags(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, subnetsFile), []byte("# home\n192.168.1.0/24\n\n10.0.0.0/8 # lab\n"), 0o600)
	o, err := parseFlags([]string{"-config", dir, "-lan", "172.16.0.0/12"}, "/h")
	if err != nil {
		t.Fatal(err)
	}
	got, err := subnets(o)
	if err != nil {
		t.Fatal(err)
	}
	var s []string
	for _, p := range got {
		s = append(s, p.String())
	}
	if !reflect.DeepEqual(s, []string{"192.168.1.0/24", "10.0.0.0/8", "172.16.0.0/12"}) {
		t.Fatalf("subnets = %v", s)
	}
	if o.lanPort != lan.DefaultPort {
		t.Fatalf("lan port %d", o.lanPort)
	}
}

func TestNoSubnetsMeansNoLANListener(t *testing.T) {
	ln, err := listenLAN(options{lanPort: 0}, nil)
	if err != nil || ln != nil {
		t.Fatalf("listenLAN with no subnets = %v, %v; want no listener", ln, err)
	}
}

func TestLANHostNames(t *testing.T) {
	got := lanHosts("Lib-2833.lan", []lan.Addr{{IP: []byte{192, 168, 1, 20}, Iface: "en0"}})
	want := []string{"192.168.1.20", "lib-2833.local", "phrased-lib-2833.local"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("lanHosts = %v, want %v", got, want)
	}
}

func TestBonjourAdvertisesOnlyLANInterfacesAndNoSecrets(t *testing.T) {
	addrs := []lan.Addr{
		{IP: []byte{192, 168, 1, 20}, Iface: "en0"},
		{IP: []byte{192, 168, 1, 21}, Iface: "en0"},
		{IP: []byte{10, 0, 0, 5}, Iface: "en7"},
	}
	cfg := bonjourConfig("Lib-2833.lan", "lib-2833.tail1234.ts.net", 47812, addrs)
	if cfg.Type != "_phrasedeck._tcp" || cfg.Port != 47812 || cfg.Host != "phrased-lib-2833" || cfg.Name != "lib-2833" {
		t.Fatalf("config %+v", cfg)
	}
	if !reflect.DeepEqual(cfg.Ifaces, []string{"en0", "en7"}) {
		t.Fatalf("ifaces %v, want only the LAN interfaces, once each", cfg.Ifaces)
	}
	want := map[string]string{"v": "1", "ts": "lib-2833.tail1234.ts.net", "port": "47812"}
	if !reflect.DeepEqual(cfg.Text, want) {
		t.Fatalf("TXT %v, want exactly %v", cfg.Text, want)
	}
	if off := bonjourConfig("mac", "", 47812, addrs); off.Text["ts"] != "" || len(off.Text) != 2 {
		t.Fatalf("with Tailscale off, TXT %v should leave out ts", off.Text)
	}
}
