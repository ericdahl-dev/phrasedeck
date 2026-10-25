package lan

import (
	"encoding/json"
	"os"
	"strings"
	"testing"
)

// Private home addresses must be the same set the app accepts in a QR code (contract/pairing.json).
func TestPrivateRangesMatchTheContract(t *testing.T) {
	b, err := os.ReadFile("../../../contract/pairing.json")
	if err != nil {
		t.Fatal(err)
	}
	var c struct {
		LANAddresses struct{ Private, NotPrivate []string }
	}
	if err := json.Unmarshal(b, &c); err != nil {
		t.Fatal(err)
	}
	host := func(addr string) []string {
		if strings.Contains(addr, ":") {
			return []string{addr + "/128"}
		}
		return []string{addr + "/32"}
	}
	for _, a := range c.LANAddresses.Private {
		if _, err := ParsePrefixes(host(a)); err != nil {
			t.Errorf("%s should be private: %v", a, err)
		}
	}
	for _, a := range c.LANAddresses.NotPrivate {
		if _, err := ParsePrefixes(host(a)); err == nil {
			t.Errorf("%s should not be private", a)
		}
	}
}
