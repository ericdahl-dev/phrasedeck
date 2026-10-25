//go:build darwin

package typer

/*
#cgo LDFLAGS: -framework ApplicationServices
#include <ApplicationServices/ApplicationServices.h>

static void postUnicode(const UniChar *buf, int n) {
	for (int down = 1; down >= 0; down--) {
		CGEventRef ev = CGEventCreateKeyboardEvent(NULL, 0, down);
		CGEventSetFlags(ev, 0); // a held modifier must not turn text into shortcuts
		CGEventKeyboardSetUnicodeString(ev, n, buf);
		CGEventPost(kCGHIDEventTap, ev);
		CFRelease(ev);
	}
}

static void postKey(CGKeyCode key, CGEventFlags flags) {
	for (int down = 1; down >= 0; down--) {
		CGEventRef ev = CGEventCreateKeyboardEvent(NULL, key, down);
		CGEventSetFlags(ev, flags);
		CGEventPost(kCGHIDEventTap, ev);
		CFRelease(ev);
	}
}

static int canPost(void) { return CGPreflightPostEventAccess(); }
*/
import "C"

import (
	"time"
	"unicode/utf16"
	"unsafe"
)

const kVKReturn = 36

// Mac types with CGEvent keyboard events. It needs the Accessibility permission.
type Mac struct{}

// New returns this OS's backend.
func New() Backend { return Mac{} }

func (Mac) Type(text string) error {
	// The planner's chunks are 20 runes; #4 re-chunks by UTF-16 units so emoji stay whole.
	u := utf16.Encode([]rune(text))
	if len(u) == 0 {
		return nil
	}
	C.postUnicode((*C.UniChar)(unsafe.Pointer(&u[0])), C.int(len(u)))
	time.Sleep(2 * time.Millisecond)
	return nil
}

func (Mac) Enter() error {
	C.postKey(kVKReturn, 0)
	return nil
}

func (Mac) ShiftEnter() error {
	C.postKey(kVKReturn, C.kCGEventFlagMaskShift)
	return nil
}

func (Mac) Paste(string) error { return ErrPasteUnsupported }

// Ready reports whether macOS lets this binary post events. It never prompts.
func (Mac) Ready() bool { return C.canPost() != 0 }
