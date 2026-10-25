package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

const socketfilterfw = "/usr/libexec/ApplicationFirewall/socketfilterfw"

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
	return firewallWarning(string(state), string(apps), exe)
}

// firewallWarning reads socketfilterfw's --getglobalstate and --listapps output.
func firewallWarning(state, apps, exe string) string {
	fix := fmt.Sprintf("sudo %s --add %s && sudo %[1]s --unblockapp %[2]s", socketfilterfw, exe)
	switch {
	case strings.Contains(state, "State = 0"):
		return ""
	case strings.Contains(state, "State = 2"):
		return "the macOS firewall blocks all incoming connections, so other devices can't reach phrased; turn off \"Block all incoming connections\" in System Settings > Network > Firewall > Options, then run: " + fix
	}
	lines := strings.Split(apps, "\n")
	for i, line := range lines {
		_, path, ok := strings.Cut(line, " : ")
		if !ok || strings.TrimSpace(path) != exe {
			continue
		}
		if i+1 < len(lines) && strings.Contains(lines[i+1], "Allow incoming") {
			return ""
		}
	}
	return "the macOS firewall is on and doesn't allow phrased, so other devices can't reach it (stealth mode drops them without a prompt); run: " + fix
}
