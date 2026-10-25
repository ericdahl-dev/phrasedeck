package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

const (
	socketfilterfw = "/usr/libexec/ApplicationFirewall/socketfilterfw"
	// managedFirewall exists when an MDM profile sets the firewall; then nobody on the Mac
	// can change it, not even with sudo.
	managedFirewall = "/Library/Managed Preferences/com.apple.security.firewall.plist"
)

// checkFirewall returns a warning when the macOS Application Firewall would drop incoming
// connections to this binary. In stealth mode it drops them silently, with no prompt.
func checkFirewall() string {
	exe, err := os.Executable()
	if err != nil {
		return ""
	}
	if p, err := filepath.EvalSymlinks(exe); err == nil {
		exe = p
	}
	state, err := exec.Command(socketfilterfw, "--getglobalstate").Output()
	if err != nil {
		return ""
	}
	apps, err := exec.Command(socketfilterfw, "--listapps").Output()
	if err != nil {
		return ""
	}
	_, err = os.Stat(managedFirewall)
	return firewallWarning(string(state), string(apps), exe, err == nil)
}

// allowedApps returns the paths --listapps marks "Allow incoming connections".
func allowedApps(apps string) []string {
	var out []string
	lines := strings.Split(apps, "\n")
	for i, line := range lines {
		_, path, ok := strings.Cut(line, " : ")
		if ok && i+1 < len(lines) && strings.Contains(lines[i+1], "Allow incoming") {
			out = append(out, strings.TrimSpace(path))
		}
	}
	return out
}

// firewallWarning reads socketfilterfw's --getglobalstate and --listapps output.
func firewallWarning(state, apps, exe string, managed bool) string {
	fix := fmt.Sprintf("sudo %s --add %s && sudo %[1]s --unblockapp %[2]s", socketfilterfw, exe)
	switch {
	case strings.Contains(state, "State = 0"):
		return ""
	case strings.Contains(state, "State = 2"):
		return "the macOS firewall blocks all incoming connections, so other devices can't reach phrased; turn off \"Block all incoming connections\" in System Settings > Network > Firewall > Options, then run: " + fix
	}
	allowed := allowedApps(apps)
	for _, path := range allowed {
		if path == exe {
			return ""
		}
	}
	if managed {
		var reuse []string
		for _, path := range allowed {
			if filepath.Base(path) == "phrased" {
				reuse = append(reuse, path)
			}
		}
		if len(reuse) > 0 {
			return "the macOS firewall is managed on this Mac and doesn't allow " + exe + ", and it can't be changed here; run phrased from a path it already allows: " + strings.Join(reuse, ", ")
		}
		return "the macOS firewall is managed on this Mac and doesn't allow phrased, and it can't be changed here, not even with sudo; ask your IT admin to allow " + exe
	}
	return "the macOS firewall is on and doesn't allow phrased, so other devices can't reach it (stealth mode drops them without a prompt); run: " + fix
}
