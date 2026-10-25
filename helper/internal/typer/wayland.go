package typer

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"
	"unicode"
)

// pasteCap is the longest a paste waits for wl-copy to serve the paste and exit.
const pasteCap = 2 * time.Second

// runner runs commands with argument lists, never through a shell.
type runner interface {
	// run runs a command to completion without capturing its output, so a child that forks
	// and keeps the pipe open (a background wl-copy) can't hang it.
	run(name string, args []string, stdin string) error
	// output captures stdout. Only for commands that don't fork (wl-paste).
	output(name string, args []string) (string, error)
	// start starts a command and returns a wait that kills it after the timeout.
	start(name string, args []string, stdin string) (wait func(time.Duration) error, err error)
}

// keyset is one keyboard tool's commands: each is the tool, then its arguments.
type keyset struct {
	tool, pkg         string // the program, and the package that installs it
	typeText          func(text string) []string
	enter, shiftEnter []string
	pasteChord        []string // Ctrl+Shift+V, the terminal paste key
	asciiOnly         bool     // can't type other characters; they're pasted
}

// wtypeKeys need the Wayland virtual-keyboard protocol: wlroots and Hyprland have it.
var wtypeKeys = keyset{
	tool:       "wtype",
	pkg:        "wtype",
	typeText:   func(text string) []string { return []string{"wtype", "--", text} }, // "--": text starting with "-" isn't an option
	enter:      []string{"wtype", "-k", "Return"},
	shiftEnter: []string{"wtype", "-M", "shift", "-k", "Return", "-m", "shift"},
	pasteChord: []string{"wtype", "-M", "ctrl", "-M", "shift", "-k", "v", "-m", "shift", "-m", "ctrl"},
}

// ydotoolKeys type through the kernel's uinput, so they work on any compositor (GNOME, KDE).
// Keys are Linux keycodes: 28 Enter, 42 left Shift, 29 left Ctrl, 47 V; :1 down, :0 up.
var ydotoolKeys = keyset{
	tool:       "ydotool",
	pkg:        "ydotool",
	typeText:   func(text string) []string { return []string{"ydotool", "type", "--", text} },
	enter:      []string{"ydotool", "key", "28:1", "28:0"},
	shiftEnter: []string{"ydotool", "key", "42:1", "28:1", "28:0", "42:0"},
	pasteChord: []string{"ydotool", "key", "29:1", "42:1", "47:1", "47:0", "42:0", "29:0"},
	asciiOnly:  true,
}

// xdotoolKeys are for X11 sessions; --clearmodifiers lets go of keys still held.
var xdotoolKeys = keyset{
	tool:       "xdotool",
	pkg:        "xdotool",
	typeText:   func(text string) []string { return []string{"xdotool", "type", "--clearmodifiers", "--", text} },
	enter:      []string{"xdotool", "key", "--clearmodifiers", "Return"},
	shiftEnter: []string{"xdotool", "key", "--clearmodifiers", "shift+Return"},
	pasteChord: []string{"xdotool", "key", "--clearmodifiers", "ctrl+shift+v"},
}

// clipset is one clipboard's commands: read it, serve text for one paste in the
// foreground, and set it (the old text, afterwards).
type clipset struct {
	read, serveOnce, set []string
	tools                [][2]string // program, package
	display              string      // the variable that says a session is there
}

var waylandClip = clipset{
	read:      []string{"wl-paste", "--no-newline", "--type", "text"},
	serveOnce: []string{"wl-copy", "--foreground", "--paste-once"},
	set:       []string{"wl-copy"},
	tools:     [][2]string{{"wl-copy", "wl-clipboard"}, {"wl-paste", "wl-clipboard"}},
	display:   "WAYLAND_DISPLAY",
}

var x11Clip = clipset{
	read:      []string{"xclip", "-selection", "clipboard", "-o"},
	serveOnce: []string{"xclip", "-selection", "clipboard", "-quiet", "-loops", "1"},
	set:       []string{"xclip", "-selection", "clipboard"},
	tools:     [][2]string{{"xclip", "xclip"}},
	display:   "DISPLAY",
}

// Desktop types with a keyboard tool and pastes with a clipboard tool: wtype on wlroots and
// Hyprland, ydotool on GNOME and KDE Wayland, xdotool on X11.
type Desktop struct {
	keys     keyset
	clip     clipset
	run      runner
	lookPath func(string) (string, error)
	getenv   func(string) string
	exists   func(path string) bool
	// settle gives wl-copy time to own the clipboard before the paste chord.
	settle time.Duration
}

// pick chooses the tools for this session: xdotool on X11, ydotool on GNOME or KDE Wayland
// (no virtual-keyboard protocol there), wtype on the rest (wlroots, Hyprland).
func pick(getenv func(string) string) *Desktop {
	keys, clip := wtypeKeys, waylandClip
	desktop := strings.ToLower(getenv("XDG_CURRENT_DESKTOP"))
	switch {
	case getenv("XDG_SESSION_TYPE") == "x11":
		keys, clip = xdotoolKeys, x11Clip
	case strings.Contains(desktop, "gnome") || strings.Contains(desktop, "kde"):
		keys = ydotoolKeys
	}
	return &Desktop{keys: keys, clip: clip, run: execRunner{}, lookPath: exec.LookPath, getenv: getenv,
		exists: func(path string) bool { _, err := os.Stat(path); return err == nil }, settle: 50 * time.Millisecond}
}

// key runs one of the keyset's commands.
func (w *Desktop) key(cmd []string) error { return w.run.run(cmd[0], cmd[1:], "") }

func (w *Desktop) Type(text string) error {
	if w.keys.asciiOnly && !isASCII(text) {
		return w.Paste(text)
	}
	return w.key(w.keys.typeText(text))
}

func isASCII(s string) bool {
	for _, r := range s {
		if r > unicode.MaxASCII {
			return false
		}
	}
	return true
}

func (w *Desktop) Enter() error { return w.key(w.keys.enter) }

func (w *Desktop) ShiftEnter() error { return w.key(w.keys.shiftEnter) }

// Paste serves text for one paste, sends Ctrl+Shift+V (the terminal paste key), waits for
// the clipboard tool to exit, and puts the old clipboard back.
// shortcut: only a text clipboard is saved and restored; an image copied before is lost.
func (w *Desktop) Paste(text string) error {
	old, err := w.run.output(w.clip.read[0], w.clip.read[1:])
	hadOld := err == nil
	wait, err := w.run.start(w.clip.serveOnce[0], w.clip.serveOnce[1:], text)
	if err != nil {
		return err
	}
	time.Sleep(w.settle)
	chordErr := w.key(w.keys.pasteChord)
	wait(pasteCap)
	if hadOld {
		if err := w.run.run(w.clip.set[0], w.clip.set[1:], old); err != nil && chordErr == nil {
			return err
		}
	}
	return chordErr
}

// Problem names a tool Type or Paste needs that isn't installed, a missing display, or
// ydotool's daemon not running.
func (w *Desktop) Problem() string {
	for _, tool := range append([][2]string{{w.keys.tool, w.keys.pkg}}, w.clip.tools...) {
		if _, err := w.lookPath(tool[0]); err != nil {
			return fmt.Sprintf("%s isn't installed (package %s)", tool[0], tool[1])
		}
	}
	if w.getenv(w.clip.display) == "" {
		return w.clip.display + " isn't set; run phrased inside your desktop session"
	}
	if w.keys.tool == "ydotool" {
		socket := w.getenv("YDOTOOL_SOCKET")
		if socket == "" {
			socket = w.getenv("XDG_RUNTIME_DIR") + "/.ydotool_socket" // ydotoold 1.0's default
		}
		if !w.exists(socket) {
			return fmt.Sprintf("ydotoold isn't running (no socket at %s); start it as the README's GNOME and KDE setup shows", socket)
		}
	}
	return ""
}

type execRunner struct{}

func command(name string, args []string, stdin string) *exec.Cmd {
	cmd := exec.Command(name, args...)
	if stdin != "" {
		cmd.Stdin = strings.NewReader(stdin)
	}
	return cmd
}

func (execRunner) run(name string, args []string, stdin string) error {
	return command(name, args, stdin).Run()
}

func (execRunner) output(name string, args []string) (string, error) {
	out, err := command(name, args, "").Output()
	return string(out), err
}

func (execRunner) start(name string, args []string, stdin string) (func(time.Duration) error, error) {
	cmd := command(name, args, stdin)
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	return func(timeout time.Duration) error {
		select {
		case err := <-done:
			return err
		case <-time.After(timeout):
			cmd.Process.Kill()
			<-done
			return errors.New(name + " didn't exit in time; killed")
		}
	}, nil
}
