package main

import (
	"encoding/json"
	"os"
	"testing"
)

// The QR code must match contract/pairing.json, which the app's tests parse too.
func TestPairURLMatchesTheContract(t *testing.T) {
	b, err := os.ReadFile("../../../contract/pairing.json")
	if err != nil {
		t.Fatal(err)
	}
	var c struct {
		Offers []struct {
			Name, Host, Code, FP, LAN, QR string
			Port, LANPort                 int
		}
	}
	if err := json.Unmarshal(b, &c); err != nil {
		t.Fatal(err)
	}
	if len(c.Offers) == 0 {
		t.Fatal("no offers in the contract")
	}
	for _, o := range c.Offers {
		if got := pairURL(o.Host, o.Port, o.Code, o.FP, o.LAN, o.LANPort); got != o.QR {
			t.Errorf("%s:\n got %s\nwant %s", o.Name, got, o.QR)
		}
	}
}
