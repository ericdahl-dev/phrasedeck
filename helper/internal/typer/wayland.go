package typer

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"
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

// Wayland types with wtype and pastes with wl-clipboard, for Hyprland.
type Wayland struct {
	run      runner
	lookPath func(string) (string, error)
	getenv   func(string) string
	// settle gives wl-copy time to own the clipboard before the paste chord.
	settle time.Duration
}

func newWayland() *Wayland {
	return &Wayland{run: execRunner{}, lookPath: exec.LookPath, getenv: os.Getenv, settle: 50 * time.Millisecond}
}

func (w *Wayland) Type(text string) error {
	return w.run.run("wtype", []string{"--", text}, "") // "--": text starting with "-" isn't an option
}

func (w *Wayland) Enter() error {
	return w.run.run("wtype", []string{"-k", "Return"}, "")
}

func (w *Wayland) ShiftEnter() error {
	return w.run.run("wtype", []string{"-M", "shift", "-k", "Return", "-m", "shift"}, "")
}

// Paste serves text for one paste with wl-copy, sends Ctrl+Shift+V (the terminal paste key),
// waits for wl-copy to exit, and puts the old clipboard back.
// shortcut: only a text clipboard is saved and restored; an image copied before is lost.
func (w *Wayland) Paste(text string) error {
	old, err := w.run.output("wl-paste", []string{"--no-newline", "--type", "text"})
	hadOld := err == nil
	wait, err := w.run.start("wl-copy", []string{"--foreground", "--paste-once"}, text)
	if err != nil {
		return err
	}
	time.Sleep(w.settle)
	chordErr := w.run.run("wtype", []string{"-M", "ctrl", "-M", "shift", "-k", "v", "-m", "shift", "-m", "ctrl"}, "")
	wait(pasteCap)
	if hadOld {
		if err := w.run.run("wl-copy", nil, old); err != nil && chordErr == nil {
			return err
		}
	}
	return chordErr
}

// Problem names a tool Type or Paste needs that isn't installed, or a missing display.
func (w *Wayland) Problem() string {
	for _, tool := range []struct{ name, pkg string }{
		{"wtype", "wtype"}, {"wl-copy", "wl-clipboard"}, {"wl-paste", "wl-clipboard"},
	} {
		if _, err := w.lookPath(tool.name); err != nil {
			return fmt.Sprintf("%s isn't installed (package %s)", tool.name, tool.pkg)
		}
	}
	if w.getenv("WAYLAND_DISPLAY") == "" {
		return "WAYLAND_DISPLAY isn't set; run phrased inside your Wayland session"
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
