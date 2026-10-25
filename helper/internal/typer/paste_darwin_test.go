//go:build darwin

package typer

import (
	"testing"
	"time"
)

// These tests use a private pasteboard, never the general one.

func TestPasteSetsTextThenRestoresEveryType(t *testing.T) {
	pb := newTestPasteboard(t)
	pb.write(map[string]string{"public.utf8-plain-text": "old", "com.example.custom": "binary-ish"})

	var during string
	err := pasteVia(pb, "a\nb", func() { during = pb.read()["public.utf8-plain-text"] }, 10*time.Millisecond)
	if err != nil {
		t.Fatal(err)
	}
	if during != "a\nb" {
		t.Fatalf("pasteboard held %q at Cmd+V, want the phrase", during)
	}
	got := pb.read()
	if got["public.utf8-plain-text"] != "old" || got["com.example.custom"] != "binary-ish" {
		t.Fatalf("not restored: %v", got)
	}
}

func TestPasteKeepsWhatWasCopiedDuringTheWait(t *testing.T) {
	pb := newTestPasteboard(t)
	pb.write(map[string]string{"public.utf8-plain-text": "old"})
	err := pasteVia(pb, "phrase", func() {
		go func() {
			time.Sleep(5 * time.Millisecond)
			pb.write(map[string]string{"public.utf8-plain-text": "copied meanwhile"})
		}()
	}, 50*time.Millisecond)
	if err != nil {
		t.Fatal(err)
	}
	if got := pb.read()["public.utf8-plain-text"]; got != "copied meanwhile" {
		t.Fatalf("pasteboard %q, want the new copy kept", got)
	}
}

func TestPasteRestoresEmptyPasteboard(t *testing.T) {
	pb := newTestPasteboard(t)
	if err := pasteVia(pb, "phrase", func() {}, 10*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	if got := pb.read(); len(got) != 0 {
		t.Fatalf("empty pasteboard came back as %v", got)
	}
}

func TestRestoreDelayIsAtLeast500ms(t *testing.T) {
	if pasteRestoreDelay < 500*time.Millisecond {
		t.Fatalf("pasteRestoreDelay %v", pasteRestoreDelay)
	}
}

func newTestPasteboard(t *testing.T) *pasteboard {
	t.Helper()
	pb := newPrivatePasteboard()
	t.Cleanup(pb.release)
	return pb
}
