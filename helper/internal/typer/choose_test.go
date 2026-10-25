package typer

import (
	"errors"
	"reflect"
	"strings"
	"testing"
)

// session builds the backend New would pick for these session variables, on a fake runner.
func session(f *fakeRunner, env map[string]string) *Desktop {
	w := pick(func(k string) string { return env[k] })
	w.run, w.settle = f, 0
	w.lookPath = func(name string) (string, error) { return "/usr/bin/" + name, nil }
	w.exists = func(string) bool { return true }
	return w
}

var gnome = map[string]string{"XDG_SESSION_TYPE": "wayland", "XDG_CURRENT_DESKTOP": "ubuntu:GNOME", "WAYLAND_DISPLAY": "wayland-0", "XDG_RUNTIME_DIR": "/run/user/1000"}

// wtype needs the virtual-keyboard protocol, which GNOME's Mutter lacks (#69), so GNOME
// types through ydotool's uinput keyboard: Enter is keycode 28 down, then up.
func TestGNOMEWaylandEntersWithYdotool(t *testing.T) {
	f := &fakeRunner{}
	if err := session(f, gnome).Enter(); err != nil {
		t.Fatal(err)
	}
	if want := []string{"ydotool key 28:1 28:0"}; !reflect.DeepEqual(f.calls, want) {
		t.Fatalf("calls %q, want %q", f.calls, want)
	}
}

// omarchy (Hyprland) keeps wtype, unchanged (#69 acceptance).
func TestHyprlandStillTypesWithWtype(t *testing.T) {
	f := &fakeRunner{}
	env := map[string]string{"XDG_SESSION_TYPE": "wayland", "XDG_CURRENT_DESKTOP": "Hyprland", "WAYLAND_DISPLAY": "wayland-1"}
	if err := session(f, env).Type("hi"); err != nil {
		t.Fatal(err)
	}
	if want := []string{"wtype -- hi"}; !reflect.DeepEqual(f.calls, want) {
		t.Fatalf("calls %q, want %q", f.calls, want)
	}
}

// ydotool sends US keycodes, so it types ASCII only. Anything else goes through the
// clipboard, the way a long or multi-line phrase does.
func TestYdotoolTypesASCIIAndPastesTheRest(t *testing.T) {
	f := &fakeRunner{clipboard: "old"}
	w := session(f, gnome)
	if err := w.Type("hello"); err != nil {
		t.Fatal(err)
	}
	if want := []string{"ydotool type -- hello"}; !reflect.DeepEqual(f.calls, want) {
		t.Fatalf("ASCII: calls %q, want %q", f.calls, want)
	}
	f.calls = nil
	if err := w.Type("naïve café ✓"); err != nil {
		t.Fatal(err)
	}
	want := []string{
		"wl-paste --no-newline --type text",
		"start wl-copy --foreground --paste-once <naïve café ✓",
		"ydotool key 29:1 42:1 47:1 47:0 42:0 29:0",
		"wait 2s",
		"wl-copy <old",
	}
	if !reflect.DeepEqual(f.calls, want) {
		t.Fatalf("non-ASCII: calls %q, want %q", f.calls, want)
	}
}

// Problem is what /health and the startup log show when the Helper isn't Ready: on GNOME it
// names ydotool and its daemon, not wtype.
func TestGNOMEProblemNamesYdotoolAndItsDaemon(t *testing.T) {
	w := session(&fakeRunner{}, gnome)
	if p := w.Problem(); p != "" {
		t.Fatalf("problem %q with ydotool, wl-clipboard and ydotoold running", p)
	}
	w.lookPath = func(name string) (string, error) {
		if name == "ydotool" {
			return "", errors.New("not found")
		}
		return "/usr/bin/" + name, nil
	}
	if p := w.Problem(); !strings.Contains(p, "ydotool isn't installed (package ydotool)") {
		t.Errorf("without ydotool: problem %q", p)
	}

	w = session(&fakeRunner{}, gnome)
	// ydotoold 1.0 (Ubuntu's package) listens at $XDG_RUNTIME_DIR/.ydotool_socket.
	w.exists = func(path string) bool { return path != "/run/user/1000/.ydotool_socket" }
	if p := w.Problem(); !strings.Contains(p, "ydotoold isn't running") {
		t.Errorf("without the daemon's socket: problem %q", p)
	}
}

var x11 = map[string]string{"XDG_SESSION_TYPE": "x11", "XDG_CURRENT_DESKTOP": "ubuntu:GNOME", "DISPLAY": ":0"}

// On X11 (any desktop) xdotool types and xclip holds the clipboard; no Wayland tool is used.
func TestX11TypesWithXdotoolAndPastesWithXclip(t *testing.T) {
	f := &fakeRunner{clipboard: "old"}
	w := session(f, x11)
	if err := w.Type("naïve café ✓"); err != nil {
		t.Fatal(err)
	}
	if err := w.Paste("two\nlines"); err != nil {
		t.Fatal(err)
	}
	if err := w.ShiftEnter(); err != nil {
		t.Fatal(err)
	}
	want := []string{
		"xdotool type --clearmodifiers -- naïve café ✓",
		"xclip -selection clipboard -o",
		"start xclip -selection clipboard -quiet -loops 1 <two\nlines",
		"xdotool key --clearmodifiers ctrl+shift+v",
		"wait 2s",
		"xclip -selection clipboard <old",
		"xdotool key --clearmodifiers shift+Return",
	}
	if !reflect.DeepEqual(f.calls, want) {
		t.Fatalf("calls\n%q\nwant\n%q", f.calls, want)
	}
	if p := w.Problem(); p != "" {
		t.Fatalf("problem %q with a display and every tool", p)
	}
	w.getenv = func(string) string { return "" }
	if p := w.Problem(); !strings.Contains(p, "DISPLAY") || strings.Contains(p, "WAYLAND") {
		t.Errorf("no display: problem %q should name DISPLAY", p)
	}
}

// The user manager may not have XDG_SESSION_TYPE; DISPLAY without WAYLAND_DISPLAY is X11.
func TestDISPLAYAloneMeansX11(t *testing.T) {
	f := &fakeRunner{}
	if err := session(f, map[string]string{"XDG_CURRENT_DESKTOP": "GNOME", "DISPLAY": ":0"}).Enter(); err != nil {
		t.Fatal(err)
	}
	if want := []string{"xdotool key --clearmodifiers Return"}; !reflect.DeepEqual(f.calls, want) {
		t.Fatalf("calls %q, want %q", f.calls, want)
	}
}

// Without XDG_RUNTIME_DIR, ydotool falls back to /tmp/.ydotool_socket; so does the check.
func TestYdotoolSocketWithoutARuntimeDirIsInTmp(t *testing.T) {
	env := map[string]string{"XDG_SESSION_TYPE": "wayland", "XDG_CURRENT_DESKTOP": "GNOME", "WAYLAND_DISPLAY": "wayland-0"}
	w := session(&fakeRunner{}, env)
	w.exists = func(path string) bool { return path != "/tmp/.ydotool_socket" }
	if p := w.Problem(); !strings.Contains(p, "/tmp/.ydotool_socket") {
		t.Errorf("problem %q should name /tmp/.ydotool_socket", p)
	}
}
