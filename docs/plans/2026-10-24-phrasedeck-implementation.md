# phrasedeck Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build `phrased`, a Go helper that types phrases into the focused window on macOS and
Hyprland, and a SwiftUI iOS app whose buttons send those phrases to it.

**Architecture:** The helper is a small authenticated HTTP service. A pure `plan` package turns a
phrase into typing steps (text chunks, Shift+Enter for newlines, an optional final Enter, or one
paste). OS-specific `typer` backends run those steps. The iOS app is a SwiftUI grid backed by
SwiftData and CloudKit. Its `HelperClient` calls `POST /type` and `GET /health`.

**Tech stack:** Go 1.22 (stdlib `net/http`, `github.com/grandcat/zeroconf`, and cgo + CoreGraphics on
macOS); `wtype` and `wl-clipboard` on Linux; Swift 6 and SwiftUI, SwiftData + CloudKit, Keychain,
and XcodeGen for the project file.

**Design:** `docs/plans/2026-10-24-phrasedeck-design.md`. Read it first.

**Rules for whoever builds this:**
- Use American spelling everywhere, in code, comments, commits and docs.
- Each commit ends with `Assisted-by: Claude Code:<model>`. Never add `Co-Authored-By`. Strip any
  `Claude-Session` trailer.
- Use TDD: write the failing test, run it and watch it fail, then implement and see it pass.
- Commit after every task. Never force-push.

---

## Part A: the `phrased` helper

### Task 1: Go module skeleton

**Files:**
- Create: `helper/go.mod`
- Create: `helper/cmd/phrased/main.go`

**Step 1:** In `helper/`, run `go mod init github.com/ericdahl-dev/phrasedeck/helper`.

**Step 2:** Create `helper/cmd/phrased/main.go`:

```go
package main

import "fmt"

func main() {
	fmt.Println("phrased")
}
```

**Step 3:** Run `cd helper && go build ./... && go vet ./...`. Expected: no output, exit 0.

**Step 4:** Commit: `git add helper && git commit -m "helper: module skeleton"`

### Task 2: `plan`, turning a phrase into steps

The heart of the helper. It's pure, so it's easy to test.

**Files:**
- Create: `helper/internal/plan/plan.go`
- Test: `helper/internal/plan/plan_test.go`

**Step 1: Write the failing tests**

```go
package plan

import (
	"reflect"
	"strings"
	"testing"
)

func TestTypeSimpleWithEnter(t *testing.T) {
	got := Build("hello", true, ModeAuto)
	want := []Step{{Kind: TypeText, Text: "hello"}, {Kind: Enter}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v want %#v", got, want)
	}
}

func TestNoEnter(t *testing.T) {
	got := Build("merge ", false, ModeAuto)
	want := []Step{{Kind: TypeText, Text: "merge "}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v want %#v", got, want)
	}
}

func TestNewlinesBecomeShiftEnter(t *testing.T) {
	got := Build("a\nb", true, ModeType)
	want := []Step{
		{Kind: TypeText, Text: "a"}, {Kind: ShiftEnter},
		{Kind: TypeText, Text: "b"}, {Kind: Enter},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v want %#v", got, want)
	}
}

func TestChunksAt20Runes(t *testing.T) {
	got := Build(strings.Repeat("é", 45), false, ModeType)
	if len(got) != 3 || len([]rune(got[0].Text)) != 20 || len([]rune(got[2].Text)) != 5 {
		t.Fatalf("bad chunks: %#v", got)
	}
}

func TestAutoPastesLongText(t *testing.T) {
	long := strings.Repeat("x", PasteThreshold+1)
	got := Build(long, true, ModeAuto)
	want := []Step{{Kind: PasteText, Text: long}, {Kind: Enter}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v", got)
	}
}

func TestForcedPasteKeepsNewlines(t *testing.T) {
	got := Build("a\nb", false, ModePaste)
	want := []Step{{Kind: PasteText, Text: "a\nb"}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v", got)
	}
}

func TestEmptyTextOnlyEnter(t *testing.T) {
	got := Build("", true, ModeAuto)
	want := []Step{{Kind: Enter}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v", got)
	}
}

func TestCRLFNormalized(t *testing.T) {
	got := Build("a\r\nb", false, ModeType)
	want := []Step{{Kind: TypeText, Text: "a"}, {Kind: ShiftEnter}, {Kind: TypeText, Text: "b"}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v", got)
	}
}
```

**Step 2:** Run `cd helper && go test ./internal/plan/`. Expected: FAIL, with `undefined: Build`.

**Step 3: Implement** `helper/internal/plan/plan.go`:

```go
// Package plan turns a phrase into the keyboard steps a typer runs.
package plan

import "strings"

type Kind int

const (
	TypeText Kind = iota
	PasteText
	ShiftEnter
	Enter
)

type Mode string

const (
	ModeAuto  Mode = "auto"
	ModeType  Mode = "type"
	ModePaste Mode = "paste"
)

// PasteThreshold is the length (in runes) above which auto mode pastes instead of typing.
const PasteThreshold = 300

// ChunkSize is how many runes go into one typing event.
const ChunkSize = 20

type Step struct {
	Kind Kind
	Text string
}

func Build(text string, enter bool, mode Mode) []Step {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	var steps []Step
	paste := mode == ModePaste || (mode == ModeAuto && len([]rune(text)) > PasteThreshold)
	if text != "" {
		if paste {
			steps = append(steps, Step{Kind: PasteText, Text: text})
		} else {
			for i, line := range strings.Split(text, "\n") {
				if i > 0 {
					steps = append(steps, Step{Kind: ShiftEnter})
				}
				steps = append(steps, chunk(line)...)
			}
		}
	}
	if enter {
		steps = append(steps, Step{Kind: Enter})
	}
	return steps
}

func chunk(s string) []Step {
	var out []Step
	r := []rune(s)
	for len(r) > 0 {
		n := min(ChunkSize, len(r))
		out = append(out, Step{Kind: TypeText, Text: string(r[:n])})
		r = r[n:]
	}
	return out
}
```

**Step 4:** Run `go test ./internal/plan/ -v`. Expected: all 8 tests PASS.

**Step 5:** Commit: `git commit -am "helper: plan phrases into typing steps"` (add the new files first).

### Task 3: the `typer` interface and a fake

**Files:**
- Create: `helper/internal/typer/typer.go`
- Create: `helper/internal/typer/fake.go`
- Test: `helper/internal/typer/typer_test.go`

**Step 1: Failing test:** `Run` passes steps to the backend in order and stops at the first error.

```go
package typer

import (
	"errors"
	"testing"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/plan"
)

func TestRunInOrder(t *testing.T) {
	f := &Fake{}
	err := Run(f, plan.Build("a\nb", true, plan.ModeType))
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"type:a", "shift-enter", "type:b", "enter"}
	if len(f.Log) != len(want) {
		t.Fatalf("got %v", f.Log)
	}
	for i := range want {
		if f.Log[i] != want[i] {
			t.Fatalf("got %v", f.Log)
		}
	}
}

func TestRunStopsOnError(t *testing.T) {
	f := &Fake{FailOn: "enter"}
	err := Run(f, plan.Build("a", true, plan.ModeType))
	if !errors.Is(err, ErrFake) {
		t.Fatalf("want ErrFake, got %v", err)
	}
}
```

**Step 2:** Run `go test ./internal/typer/`. Expected: FAIL, `undefined: Run`.

**Step 3: Implement.** `typer.go`:

```go
// Package typer sends keystrokes to the focused window.
package typer

import "github.com/ericdahl-dev/phrasedeck/helper/internal/plan"

// Backend is one OS's way of typing.
type Backend interface {
	Type(text string) error
	Paste(text string) error
	ShiftEnter() error
	Enter() error
	// Ready reports whether the backend can type (e.g. macOS Accessibility granted).
	Ready() bool
}

func Run(b Backend, steps []plan.Step) error {
	for _, s := range steps {
		var err error
		switch s.Kind {
		case plan.TypeText:
			err = b.Type(s.Text)
		case plan.PasteText:
			err = b.Paste(s.Text)
		case plan.ShiftEnter:
			err = b.ShiftEnter()
		case plan.Enter:
			err = b.Enter()
		}
		if err != nil {
			return err
		}
	}
	return nil
}
```

`fake.go`:

```go
package typer

import "errors"

var ErrFake = errors.New("fake failure")

// Fake records calls; used in tests.
type Fake struct {
	Log    []string
	FailOn string
	NotReady bool
}

func (f *Fake) rec(s string) error {
	if f.FailOn != "" && s == f.FailOn {
		return ErrFake
	}
	f.Log = append(f.Log, s)
	return nil
}

func (f *Fake) Type(t string) error  { return f.rec("type:" + t) }
func (f *Fake) Paste(t string) error { return f.rec("paste:" + t) }
func (f *Fake) ShiftEnter() error    { return f.rec("shift-enter") }
func (f *Fake) Enter() error         { return f.rec("enter") }
func (f *Fake) Ready() bool          { return !f.NotReady }
```

**Step 4:** Run `go test ./internal/typer/ -v`. Expected: PASS.

**Step 5:** Commit: `helper: typer interface and fake`.

### Task 4: the token store and pairing codes

**Files:**
- Create: `helper/internal/auth/store.go`
- Test: `helper/internal/auth/store_test.go`

**Behavior:**
- `NewStore(path)` loads the file, or starts empty.
- `StartPairing()` returns a 6-digit code that is valid for 2 minutes. Only one code is live at a
  time.
- `Pair(code, deviceName)` returns a random 32-byte token encoded as hex, and stores only its
  SHA-256. A wrong or expired code returns `ErrBadCode`. Five wrong attempts cancel the code.
- `Check(token)` compares the hash against the stored hashes in constant time
  (`subtle.ConstantTimeCompare`).
- Saves write to the file with mode 0600 through a temp file and a rename.

**Step 1: Failing tests:**

```go
package auth

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func newStore(t *testing.T) (*Store, string) {
	p := filepath.Join(t.TempDir(), "tokens.json")
	s, err := NewStore(p)
	if err != nil {
		t.Fatal(err)
	}
	return s, p
}

func TestPairAndCheck(t *testing.T) {
	s, p := newStore(t)
	code := s.StartPairing()
	if len(code) != 6 {
		t.Fatalf("code %q", code)
	}
	tok, err := s.Pair(code, "iPhone")
	if err != nil {
		t.Fatal(err)
	}
	if !s.Check(tok) || s.Check("nope") {
		t.Fatal("check wrong")
	}
	fi, _ := os.Stat(p)
	if fi.Mode().Perm() != 0o600 {
		t.Fatalf("mode %v", fi.Mode().Perm())
	}
	s2, _ := NewStore(p) // persisted
	if !s2.Check(tok) {
		t.Fatal("not persisted")
	}
}

func TestCodeSingleUse(t *testing.T) {
	s, _ := newStore(t)
	code := s.StartPairing()
	if _, err := s.Pair(code, "a"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Pair(code, "b"); err != ErrBadCode {
		t.Fatalf("reuse allowed: %v", err)
	}
}

func TestCodeExpires(t *testing.T) {
	s, _ := newStore(t)
	now := time.Now()
	s.now = func() time.Time { return now }
	code := s.StartPairing()
	s.now = func() time.Time { return now.Add(3 * time.Minute) }
	if _, err := s.Pair(code, "a"); err != ErrBadCode {
		t.Fatalf("expired code accepted: %v", err)
	}
}

func TestFiveWrongCancels(t *testing.T) {
	s, _ := newStore(t)
	code := s.StartPairing()
	for i := 0; i < 5; i++ {
		s.Pair("000000x", "a")
	}
	if _, err := s.Pair(code, "a"); err != ErrBadCode {
		t.Fatal("code survived 5 bad tries")
	}
}

func TestFileHasNoPlainToken(t *testing.T) {
	s, p := newStore(t)
	tok, _ := s.Pair(s.StartPairing(), "a")
	b, _ := os.ReadFile(p)
	if contains(string(b), tok) {
		t.Fatal("plain token on disk")
	}
}

func contains(h, n string) bool { return len(n) > 0 && len(h) >= len(n) && (indexOf(h, n) >= 0) }
func indexOf(h, n string) int {
	for i := 0; i+len(n) <= len(h); i++ {
		if h[i:i+len(n)] == n {
			return i
		}
	}
	return -1
}
```

(The builder may replace `contains` with `strings.Contains`.)

**Step 2:** Run it. Expected: FAIL, `undefined: NewStore`.

**Step 3: Implement** `store.go` to the behavior above. Use `crypto/rand` for the code and the
token, `crypto/sha256` and `encoding/hex`, a `sync.Mutex`, and `now func() time.Time` defaulting to
`time.Now`. The JSON shape is `{"devices":[{"name":"iPhone","sha256":"…","paired_at":"…"}]}`.

**Step 4:** Run `go test ./internal/auth/ -v`. Expected: PASS.

**Step 5:** Commit: `helper: token store and pairing codes`.

### Task 5: the HTTP server

**Files:**
- Create: `helper/internal/server/server.go`
- Test: `helper/internal/server/server_test.go`

**Endpoints:**
- `GET /health` takes no auth and returns `{"hostname","os","accessibility_ok"}`. It holds nothing
  secret and lets the app show the status dot before pairing.
- `POST /pair/start` takes no auth. It calls `StartPairing()` and the `OnCode(code)` callback, which
  shows a notification or logs the code. It returns 204, and the code never goes in the response.
- `POST /pair` with `{"code","device"}` returns `{"token"}`, or 401.
- `POST /type` needs `Authorization: Bearer <token>`. It takes
  `{"text","enter","mode"}`, returning 200 `{"ok":true}`, 401, 400 for a bad mode, 413 for text over
  10,000 runes, 429 when rate-limited, 503 when the backend isn't ready, or 500 on a typer error.
- Rate limit: a token bucket of 10 per second with a burst of 10, shared across all callers.
- Typing is serialized with a mutex, so two phrases never interleave.

**Step 1: Failing tests** use `httptest` with `typer.Fake` and a temp `auth.Store`:

```go
func TestTypeNeedsAuth(t *testing.T)       // no header -> 401, fake.Log empty
func TestTypeOK(t *testing.T)              // valid token, {"text":"hi","enter":true} -> 200, Log = [type:hi enter]
func TestTypeBadMode(t *testing.T)         // mode "x" -> 400
func TestTypeTooLong(t *testing.T)         // 10001 runes -> 413
func TestTypeNotReady(t *testing.T)        // Fake.NotReady -> 503, and /health accessibility_ok=false
func TestRateLimit(t *testing.T)           // 11 rapid requests -> at least one 429
func TestPairFlow(t *testing.T)            // /pair/start -> OnCode gets code; /pair with it -> token works on /type
func TestPairStartDoesNotLeakCode(t *testing.T) // response body of /pair/start is empty
```

Write each test in full, in the style of Task 4. Build JSON with `bytes.NewBufferString`.

**Step 2:** Run them. Expected: FAIL.

**Step 3: Implement** `server.New(cfg Config) http.Handler`, where
`Config{Store *auth.Store; Typer typer.Backend; OnCode func(string); Hostname, OS string}`.
Use `golang.org/x/time/rate` for the limiter (run `go get golang.org/x/time/rate`).

**Step 4:** Run `go test ./... -v`. Expected: PASS.

**Step 5:** Commit: `helper: http server with auth, limits and pairing`.

### Task 6: interface binding and Bonjour

**Files:**
- Create: `helper/internal/netbind/netbind.go`
- Test: `helper/internal/netbind/netbind_test.go`
- Create: `helper/internal/mdns/mdns.go`

**Step 1: Failing test** for `netbind.Pick(addrs []net.IP) []net.IP`. It keeps the Tailscale CGNAT
range `100.64.0.0/10` and private LAN ranges (`10/8`, `172.16/12`, `192.168/16`). It drops loopback,
public addresses and IPv6 link-local. Test with a table of IPs.

**Step 2:** Run it and watch it fail. **Step 3:** Implement it using `net.IPNet.Contains`.
**Step 4:** Run it and watch it pass.

**Step 5:** Write `mdns.Advertise(port int, hostname string) (stop func(), err error)` with
`zeroconf.Register("phrased-"+hostname, "_phrased._tcp", "local.", port, []string{"v=1"}, nil)`
(run `go get github.com/grandcat/zeroconf`). It has no unit test; it's checked by hand in Task 10.

**Step 6:** Commit: `helper: bind only to tailnet and LAN, advertise via Bonjour`.

### Task 7: the macOS typer backend

**Files:**
- Create: `helper/internal/typer/darwin.go` (`//go:build darwin`)

**Implementation notes:**
- Use cgo with `#cgo LDFLAGS: -framework ApplicationServices -framework AppKit`.
- **Type(text):** convert to UTF-16. For each chunk, call `CGEventCreateKeyboardEvent(nil, 0, true)`,
  then `CGEventKeyboardSetUnicodeString(ev, n, buf)`, `CGEventPost(kCGHIDEventTap, ev)`, the same
  for key-up, and `CFRelease`. Sleep 2 ms between chunks.
- **Enter:** virtual keycode 36 (`kVK_Return`), down then up. **ShiftEnter:** the same key with
  `CGEventSetFlags(ev, kCGEventFlagMaskShift)`.
- **Paste:** use `NSPasteboard` to save the current string, set the new text, send Cmd+V (keycode
  9 with `kCGEventFlagMaskCommand`), wait 150 ms, then restore the old string.
- **Ready:** `AXIsProcessTrusted()`.

**Test:** a build-only check, `GOOS=darwin go vet ./...`. Typing is checked by hand in Task 10.

**Commit:** `helper: macOS CGEvent typer`.

### Task 8: the Linux typer backend

**Files:**
- Create: `helper/internal/typer/linux.go` (`//go:build linux`)
- Test: `helper/internal/typer/linux_test.go` (`//go:build linux`)

**Implementation notes:**
- Build every command with `exec.Command` and argument slices, never `sh -c`.
- **Type(text):** `wtype -- <text>`.
- **Enter:** `wtype -k Return`. **ShiftEnter:** `wtype -M shift -k Return -m shift`.
- **Paste:** run `wl-paste --no-newline` to save the clipboard (allow it to fail), `wl-copy -- <text>`,
  `wtype -M ctrl -M shift -k v -m shift -m ctrl`, sleep 150 ms, then restore the clipboard with
  `wl-copy` (or `wl-copy --clear` if there was nothing).
- **Ready:** `exec.LookPath("wtype")` succeeds and `WAYLAND_DISPLAY` is set.
- Make the runner injectable (`var run = func(name string, args ...string) error`), so the test can
  record the argument lists.

**Step 1: Failing test:** with the runner swapped out, check that `Type("a b")` runs
`["wtype","--","a b"]`, that `ShiftEnter` has the expected arguments, and that text starting with
`-` stays after `--`.
**Steps 2 to 4:** watch it fail, implement it, watch it pass (`GOOS=linux go test` runs in CI).
**Step 5:** Commit: `helper: Linux wtype typer`.

### Task 9: wire up `main`

**Files:**
- Modify: `helper/cmd/phrased/main.go`

**Behavior:**
- Flags: `-port 47811` and `-config ~/.config/phrased`.
- Steps: pick the backend by `runtime.GOOS`, load the store, find the addresses with
  `netbind.Pick`, and start one `http.Server` per address on the port, all with the same handler.
  Then advertise with mdns and log `listening on <ip>:<port>`.
- `OnCode`: on macOS, run `osascript -e 'display notification "Code: 123456" with title "phrased"'`,
  with the code passed in a separate argument from a fixed script (`-e` script plus `on run argv`).
  On Linux, run `notify-send phrased "Code: 123456"` if it exists. Always log the code too.
- Shut down cleanly on SIGINT and SIGTERM.

**Test:** run `go build ./... && go vet ./... && go test ./...`, all clean. **Commit:**
`helper: main wiring`.

### Task 10: install files and a by-hand check

**Files:**
- Create: `helper/install/dev.ericdahl.phrased.plist` (a LaunchAgent with RunAtLoad and KeepAlive,
  running `~/.local/bin/phrased`, with logs in `~/Library/Logs/phrased.log`)
- Create: `helper/install/phrased.service` (a systemd user unit with
  `After=graphical-session.target`, `Environment=WAYLAND_DISPLAY=wayland-1`, adjusted to match
  Hyprland, and `Restart=on-failure`)
- Create: `helper/Makefile` with `build`, `test`, `install-mac` and `install-linux` targets
- Create: `README.md` at the repo root: what it is, how to install each part, and pairing

**By-hand check (record the results in the PR):**
1. On the Mac: run `make install-mac`, grant Accessibility to `phrased`, and check that
   `curl localhost`… is refused (it isn't bound there), while
   `curl http://<tailnet-ip>:47811/health` returns `accessibility_ok: true`.
2. Pair with curl: `POST /pair/start`, read the code from the notification, `POST /pair`, then
   `POST /type` with `{"text":"naïve café ✓\nline two","enter":false}` into a TextEdit window. Both
   lines should arrive, with the newline typed as Shift+Enter.
3. On omarchy: run `pacman -S wtype wl-clipboard` and `make install-linux`, then repeat steps 1 and 2
   into a terminal running Claude Code.

**Commit:** `helper: install files and README`.

### Task 11: CI

**Files:**
- Create: `.github/workflows/helper.yml`. On a push or PR, `ubuntu-latest` runs
  `go test ./...` with `-race`, plus `go vet` and `gofmt -l`, and `macos-latest` runs
  `go build ./...`.
- Make sure the org's required spelling workflow passes. If the repo doesn't pick it up on its own,
  copy its caller from another ericdahl-dev repo.

**Commit:** `ci: helper tests on linux and macos build`.

---

## Part B: the iOS app

Set the bundle ID to `dev.ericdahl.phrasedeck`, with iOS 18 or later, iPhone and iPad. Installs go
straight from Xcode with Eric's developer team; there's no TestFlight.

### Task 12: project skeleton (XcodeGen)

**Files:**
- Create: `ios/project.yml`. Define the app target `Phrasedeck` and the test target `PhrasedeckTests`,
  with the iCloud (CloudKit) capability on the container `iCloud.dev.ericdahl.phrasedeck`, and
  `NSLocalNetworkUsageDescription` plus `NSBonjourServices: [_phrased._tcp]` in Info.plist.
- Create: `ios/Phrasedeck/PhrasedeckApp.swift` and `ContentView.swift` (placeholder text).
- Add `ios/*.xcodeproj` to `.gitignore`; the project file is generated.

**Check:** `cd ios && xcodegen && xcodebuild -scheme Phrasedeck -destination 'generic/platform=iOS Simulator' build`
succeeds. **Commit:** `ios: XcodeGen skeleton`.

### Task 13: the data model and starter phrases

**Files:**
- Create: `ios/Phrasedeck/Model.swift`
- Test: `ios/PhrasedeckTests/ModelTests.swift`

**Model:** SwiftData `@Model`s, with every property defaulted or optional, as CloudKit requires.
- `Phrase(label, text, pressEnter, confirm, colorName, order, page)`
- `Page(name, order, phrases)`
- `Helper(name, host, port)`

`Seed.insertIfEmpty(context)` adds one "Agents" page with the five starter phrases from the
design's table.

**TDD:** first write the tests, using an in-memory `ModelContainer`: seeding runs once, a second
call adds nothing, the starter phrases have the right Enter and confirm flags, and phrases sort by
`order`. Watch them fail, implement, watch them pass. **Commit:** `ios: model and starter phrases`.

### Task 14: the Keychain token store

**Files:**
- Create: `ios/Phrasedeck/TokenStore.swift`
- Test: `ios/PhrasedeckTests/TokenStoreTests.swift`

**Behavior:** `set(token, for helperID)`, `get(helperID)` and `delete(helperID)`, using a
`kSecClassGenericPassword` item with service `dev.ericdahl.phrasedeck` and account `helperID`, and
`kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`, so the token never syncs.

**TDD:** a round trip; overwriting replaces the old value; deleting removes it. **Commit:**
`ios: keychain token store`.

### Task 15: `HelperClient`

**Files:**
- Create: `ios/Phrasedeck/HelperClient.swift`
- Test: `ios/PhrasedeckTests/HelperClientTests.swift`, with a `URLProtocol` stub

**API:**
- `health() async throws -> Health`
- `startPairing() async throws`
- `pair(code:device:) async throws -> String`
- `type(text:enter:mode:) async throws`
- A 3-second timeout, and **no retries**.
- Errors map to `.unreachable`, `.unauthorized`, `.notReady` (503), `.rateLimited` (429) and
  `.server(Int)`.

**TDD:** each status code maps to the right error; `type` sends the Bearer header and the JSON
body; a timeout comes back as `.unreachable`; and it makes exactly one request per call, which
proves there are no retries. **Commit:** `ios: helper client`.

### Task 16: the button grid

**Files:**
- Create: `ios/Phrasedeck/DeckView.swift`, `PhraseButton.swift`, `HelperPicker.swift`
- Modify: `ContentView.swift`

**Behavior:**
- A paged `TabView`, one page per `Page`, each holding a `LazyVGrid` of `PhraseButton`s: 3 columns
  on an iPhone, adaptive on an iPad.
- A tap fires `UIImpactFeedbackGenerator(.light)` and calls `client.type`. A green flash means
  success; a red flash plus a banner shows the error text. Phrases with `confirm` need a
  double-tap: the first tap shows "tap again", which cancels after 1.5 seconds.
- The status dot in `HelperPicker` polls `/health` every 5 seconds while the app is in the
  foreground.

**Tests:** a UI test in `PhrasedeckUITests` that starts the app with a launch argument selecting a
stub client. It checks that a tap sends exactly once, and that a confirm phrase sends only on the
second tap. **Commit:** `ios: deck grid and send feedback`.

### Task 17: the phrase editor, adding computers and pairing

**Files:**
- Create: `ios/Phrasedeck/PhraseEditor.swift`, `AddHelperView.swift`

**Behavior:**
- Long-press opens the editor sheet: label, multi-line text, the Enter and Confirm toggles, a color
  picker from 6 named colors, a page picker, and Delete. A "+" tile adds a phrase.
- **Add a computer:** a list of Bonjour results (`NWBrowser` for `_phrased._tcp`) plus manual host
  entry, for example `omarchy-laptop`. Choosing one calls `startPairing()`, asks for the 6-digit
  code, calls `pair`, and saves the token with `TokenStore`.

**Check:** run on Eric's iPhone from Xcode. Pair with the Mac and with omarchy, then tap
"Agent status" into a Claude Code terminal on each. **Commit:** `ios: editor and pairing`.

### Task 18: wrap up

- Update the README with the iOS install steps: open the generated project, choose Eric's team,
  run on the device, and reinstall when the profile expires after a year.
- Fill in the design's manual checklist on both machines, Unicode and multi-line included, and
  paste the results into the PR.
- Open a PR from the working branch and ask Eric to review. Don't merge.
