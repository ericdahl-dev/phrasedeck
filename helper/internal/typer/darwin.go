//go:build darwin

package typer

/*
#cgo CFLAGS: -x objective-c -fno-objc-arc
#cgo LDFLAGS: -framework ApplicationServices -framework AppKit
#include <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>

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

// A NULL pasteboard means the general one.
static NSPasteboard *board(void *pb) {
	return pb ? (NSPasteboard *)pb : [NSPasteboard generalPasteboard];
}

static void *pbPrivate(void) {
	return [[NSPasteboard pasteboardWithUniqueName] retain];
}

static void pbRelease(void *pb) {
	[(NSPasteboard *)pb releaseGlobally];
	[(NSPasteboard *)pb release];
}

// pbSave copies every item on the pasteboard, with the data for each of its types.
// The caller frees the result with pbFree.
static void *pbSave(void *pb) {
	@autoreleasepool {
		NSMutableArray *saved = [[NSMutableArray alloc] init];
		for (NSPasteboardItem *item in [board(pb) pasteboardItems]) {
			NSPasteboardItem *copy = [[[NSPasteboardItem alloc] init] autorelease];
			for (NSPasteboardType type in [item types]) {
				NSData *data = [item dataForType:type];
				if (data) [copy setData:data forType:type];
			}
			[saved addObject:copy];
		}
		return saved;
	}
}

static void pbFree(void *saved) { [(NSMutableArray *)saved release]; }

// pbSetString replaces the pasteboard with text and returns the new change count.
static long pbSetString(void *pb, const char *text) {
	@autoreleasepool {
		NSPasteboard *p = board(pb);
		[p clearContents];
		[p setString:[NSString stringWithUTF8String:text] forType:NSPasteboardTypeString];
		return [p changeCount];
	}
}

// pbRestore puts saved back, but only if the change count is still count, so a copy made
// since the paste is kept. It returns 1 if it restored.
static int pbRestore(void *pb, void *saved, long count) {
	@autoreleasepool {
		NSPasteboard *p = board(pb);
		if ([p changeCount] != count) return 0;
		[p clearContents];
		NSArray *items = (NSArray *)saved;
		if ([items count] > 0) [p writeObjects:items];
		return 1;
	}
}

// Test helpers: write one item with these types, and read every type of every item as UTF-8.
static void pbWrite(void *pb, const char **types, const char **values, int n) {
	@autoreleasepool {
		NSPasteboard *p = board(pb);
		[p clearContents];
		NSPasteboardItem *item = [[[NSPasteboardItem alloc] init] autorelease];
		for (int i = 0; i < n; i++) {
			[item setString:[NSString stringWithUTF8String:values[i]] forType:[NSString stringWithUTF8String:types[i]]];
		}
		[p writeObjects:@[item]];
	}
}

static char *pbDump(void *pb) {
	@autoreleasepool {
		NSMutableString *out = [NSMutableString string];
		for (NSPasteboardItem *item in [board(pb) pasteboardItems]) {
			for (NSPasteboardType type in [item types]) {
				NSString *v = [item stringForType:type] ?: @"";
				[out appendFormat:@"%@\x1f%@\x1e", type, v];
			}
		}
		return strdup([out UTF8String]);
	}
}
*/
import "C"

import (
	"strings"
	"time"
	"unsafe"
)

const (
	kVKReturn = 36
	kVKANSIV  = 9
	// maxUnits is the most UTF-16 units one keyboard event carries.
	maxUnits = 20
)

// pasteRestoreDelay is how long the phrase stays on the pasteboard before the old contents
// come back. The target app reads it after Cmd+V arrives, so this can't be too short.
var pasteRestoreDelay = 500 * time.Millisecond

// Mac types with CGEvent keyboard events. It needs the Accessibility permission.
type Mac struct{}

// New returns this OS's backend.
func New() Backend { return Mac{} }

func (Mac) Type(text string) error {
	for _, u := range utf16Chunks(text, maxUnits) {
		C.postUnicode((*C.UniChar)(unsafe.Pointer(&u[0])), C.int(len(u)))
		time.Sleep(2 * time.Millisecond)
	}
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

// Paste puts text on the pasteboard, sends Cmd+V, and then puts back what was there.
// shortcut: Cmd+V uses the ANSI V key code, so a layout that moves V (Dvorak) needs a fix.
func (Mac) Paste(text string) error {
	return pasteVia(nil, text, func() { C.postKey(kVKANSIV, C.kCGEventFlagMaskCommand) }, pasteRestoreDelay)
}

// Problem says to grant Accessibility unless macOS lets this binary post events. It never
// prompts.
func (Mac) Problem() string {
	if C.canPost() != 0 {
		return ""
	}
	return "grant Accessibility to phrased (System Settings > Privacy & Security > Accessibility)"
}

// pasteboard is an NSPasteboard; nil is the general one.
type pasteboard struct{ p unsafe.Pointer }

func (pb *pasteboard) ptr() unsafe.Pointer {
	if pb == nil {
		return nil
	}
	return pb.p
}

// pasteVia saves every item on pb, sets text, calls send, waits, and restores the saved items
// unless the pasteboard changed in the meantime.
func pasteVia(pb *pasteboard, text string, send func(), wait time.Duration) error {
	saved := C.pbSave(pb.ptr())
	defer C.pbFree(saved)
	ctext := C.CString(text)
	defer C.free(unsafe.Pointer(ctext))
	count := C.pbSetString(pb.ptr(), ctext)
	send()
	time.Sleep(wait)
	C.pbRestore(pb.ptr(), saved, count)
	return nil
}

func newPrivatePasteboard() *pasteboard { return &pasteboard{C.pbPrivate()} }

func (pb *pasteboard) release() { C.pbRelease(pb.p) }

// write replaces the pasteboard with one item holding these types. Used by tests.
func (pb *pasteboard) write(types map[string]string) {
	var ts, vs []*C.char
	for t, v := range types {
		ts, vs = append(ts, C.CString(t)), append(vs, C.CString(v))
	}
	defer func() {
		for i := range ts {
			C.free(unsafe.Pointer(ts[i]))
			C.free(unsafe.Pointer(vs[i]))
		}
	}()
	if len(ts) == 0 {
		C.pbWrite(pb.ptr(), nil, nil, 0)
		return
	}
	C.pbWrite(pb.ptr(), &ts[0], &vs[0], C.int(len(ts)))
}

// read returns every type on the pasteboard and its value as text. Used by tests.
func (pb *pasteboard) read() map[string]string {
	cs := C.pbDump(pb.ptr())
	defer C.free(unsafe.Pointer(cs))
	out := map[string]string{}
	for _, entry := range strings.Split(C.GoString(cs), "\x1e") {
		if t, v, ok := strings.Cut(entry, "\x1f"); ok {
			out[t] = v
		}
	}
	return out
}
