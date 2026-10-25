package main

import (
	"strings"
	"testing"
)

const listApps = `Total number of apps = 3 
1 : /opt/homebrew/bin/python3 
             (Allow incoming connections)
2 : /Users/eric/helper/phrased 
             (Block incoming connections)
3 : /Users/eric/other/phrased 
             (Allow incoming connections)
`

func TestFirewallWarning(t *testing.T) {
	on := "Firewall is enabled. (State = 1)\n"
	off := "Firewall is disabled. (State = 0)\n"
	for _, c := range []struct {
		name, state, exe string
		warn             bool
	}{
		{"off", off, "/Users/eric/new/phrased", false},
		{"on, not listed", on, "/Users/eric/new/phrased", true},
		{"on, blocked", on, "/Users/eric/helper/phrased", true},
		{"on, allowed", on, "/Users/eric/other/phrased", false},
		{"block all", "Firewall is blocking all non-essential incoming connections. (State = 2)\n", "/Users/eric/other/phrased", true},
	} {
		got := firewallWarning(c.state, listApps, c.exe, false)
		if (got != "") != c.warn {
			t.Errorf("%s: warning %q, want warn=%v", c.name, got, c.warn)
		}
		if c.warn && !strings.Contains(got, "socketfilterfw --add "+c.exe) {
			t.Errorf("%s: warning doesn't give the fix: %q", c.name, got)
		}
	}
}

func TestFirewallWarningOnAManagedMac(t *testing.T) {
	got := firewallWarning("Firewall is enabled. (State = 1)\n", listApps, "/Users/eric/new/phrased", true)
	if !strings.Contains(got, "managed") || !strings.Contains(got, "/Users/eric/other/phrased") {
		t.Fatalf("managed warning should say so and name the allowed path: %q", got)
	}
	if strings.Contains(got, "sudo") {
		t.Fatalf("managed warning offers sudo, which can't work: %q", got)
	}
	if got := firewallWarning("Firewall is enabled. (State = 1)\n", listApps, "/Users/eric/other/phrased", true); got != "" {
		t.Fatalf("allowed path on a managed Mac warned: %q", got)
	}
}
