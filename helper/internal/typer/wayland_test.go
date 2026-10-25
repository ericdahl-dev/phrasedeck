package typer

import (
	"errors"
	"reflect"
	"strings"
	"testing"
	"time"
)

// fakeRunner records every command as "name arg arg ... <stdin" so tests can check exact
// argument lists and their order.
type fakeRunner struct {
	calls     []string
	clipboard string // what wl-paste returns; "" means it fails (nothing copied)
	failRun   string // a call that fails
}

func rec(name string, args []string, stdin string) string {
	s := strings.Join(append([]string{name}, args...), " ")
	if stdin != "" {
		s += " <" + stdin
	}
	return s
}

func (f *fakeRunner) run(name string, args []string, stdin string) error {
	c := rec(name, args, stdin)
	f.calls = append(f.calls, c)
	if c == f.failRun {
		return errors.New("failed")
	}
	return nil
}

func (f *fakeRunner) output(name string, args []string) (string, error) {
	f.calls = append(f.calls, rec(name, args, ""))
	if f.clipboard == "" {
		return "", errors.New("Nothing is copied")
	}
	return f.clipboard, nil
}

func (f *fakeRunner) start(name string, args []string, stdin string) (func(time.Duration) error, error) {
	f.calls = append(f.calls, "start "+rec(name, args, stdin))
	return func(d time.Duration) error {
		f.calls = append(f.calls, "wait "+d.String())
		return nil
	}, nil
}

func newTestWayland(f *fakeRunner) *Wayland {
	return &Wayland{
		run:      f,
		lookPath: func(string) (string, error) { return "/usr/bin/wtype", nil },
		getenv:   func(string) string { return "wayland-1" },
		settle:   0,
	}
}

func wantCalls(t *testing.T, f *fakeRunner, want ...string) {
	t.Helper()
	if !reflect.DeepEqual(f.calls, want) {
		t.Fatalf("calls\n got %q\nwant %q", f.calls, want)
	}
}

func TestWaylandType(t *testing.T) {
	f := &fakeRunner{}
	w := newTestWayland(f)
	w.Type("a b")
	w.Type("-k Return")
	w.Type("naïve café ✓")
	wantCalls(t, f, "wtype -- a b", "wtype -- -k Return", "wtype -- naïve café ✓")
}

func TestWaylandKeys(t *testing.T) {
	f := &fakeRunner{}
	w := newTestWayland(f)
	w.Enter()
	w.ShiftEnter()
	wantCalls(t, f, "wtype -k Return", "wtype -M shift -k Return -m shift")
}

func TestWaylandPasteOrderAndRestore(t *testing.T) {
	f := &fakeRunner{clipboard: "old"}
	if err := newTestWayland(f).Paste("a\nb"); err != nil {
		t.Fatal(err)
	}
	wantCalls(t, f,
		"wl-paste --no-newline --type text",
		"start wl-copy --foreground --paste-once <a\nb",
		"wtype -M ctrl -M shift -k v -m shift -m ctrl",
		"wait 2s",
		"wl-copy <old",
	)
}

func TestWaylandPasteWithNothingCopiedRestoresNothing(t *testing.T) {
	f := &fakeRunner{}
	if err := newTestWayland(f).Paste("x"); err != nil {
		t.Fatal(err)
	}
	wantCalls(t, f,
		"wl-paste --no-newline --type text",
		"start wl-copy --foreground --paste-once <x",
		"wtype -M ctrl -M shift -k v -m shift -m ctrl",
		"wait 2s",
	)
}

func TestWaylandPasteChordFailureStillRestores(t *testing.T) {
	f := &fakeRunner{clipboard: "old", failRun: "wtype -M ctrl -M shift -k v -m shift -m ctrl"}
	if err := newTestWayland(f).Paste("x"); err == nil {
		t.Fatal("want the chord's error")
	}
	if last := f.calls[len(f.calls)-1]; last != "wl-copy <old" {
		t.Fatalf("clipboard not restored after a failed chord: %q", f.calls)
	}
}

func TestWaylandReady(t *testing.T) {
	w := newTestWayland(&fakeRunner{})
	if !w.Ready() {
		t.Fatal("not ready with wtype and a display")
	}
	w.getenv = func(string) string { return "" }
	if w.Ready() {
		t.Fatal("ready without WAYLAND_DISPLAY")
	}
	w = newTestWayland(&fakeRunner{})
	w.lookPath = func(string) (string, error) { return "", errors.New("not found") }
	if w.Ready() {
		t.Fatal("ready without wtype")
	}
}

func TestExecRunnerStartWaitAndKill(t *testing.T) {
	wait, err := execRunner{}.start("sh", []string{"-c", "cat >/dev/null"}, "served")
	if err != nil {
		t.Fatal(err)
	}
	if err := wait(2 * time.Second); err != nil {
		t.Fatalf("wait: %v", err)
	}
	wait, err = execRunner{}.start("sleep", []string{"5"}, "")
	if err != nil {
		t.Fatal(err)
	}
	start := time.Now()
	if err := wait(50 * time.Millisecond); err == nil {
		t.Fatal("want an error for a command killed at the cap")
	}
	if time.Since(start) > time.Second {
		t.Fatal("wait didn't stop at the cap")
	}
}

func TestExecRunnerOutputPassesArgsWithoutAShell(t *testing.T) {
	out, err := execRunner{}.output("printf", []string{"%s", "-- $HOME; x"})
	if err != nil || out != "-- $HOME; x" {
		t.Fatalf("got %q %v", out, err)
	}
}
