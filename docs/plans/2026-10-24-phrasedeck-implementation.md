# phrasedeck Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build `phrased`, a Go helper that types phrases into the focused window on macOS and
Hyprland, and a SwiftUI iOS app whose buttons send those phrases to it over the tailnet.

**Architecture:** The helper is a small authenticated HTTP service that only accepts connections
from Tailscale addresses. A pure `plan` package turns a phrase into typing steps (text chunks,
Shift+Enter for newlines in `type` mode, an optional final Enter, or one paste). OS-specific
`typer` backends run those steps. The iOS app is a SwiftUI grid backed by SwiftData (local first,
CloudKit later). Its `HelperClient` calls `POST /type`, `POST /pair` and `GET /health`.

**Tech stack:** Go 1.22 (stdlib `net/http`, `golang.org/x/time/rate`, and cgo + CoreGraphics/AppKit
on macOS); `wtype` and `wl-clipboard` on Linux; Swift 6 and SwiftUI, SwiftData, Keychain, and
XcodeGen for the project file; internal TestFlight for installs.

**Design:** `docs/plans/2026-10-24-phrasedeck-design.md`. Read it first.

**Issues:** PRD #2. Every task below names the one issue it belongs to. Issue #11 (Bonjour
discovery) is deferred and has no task.

| Issue | Tasks |
|---|---|
| #3 curl typing on the Mac | 1 to 9 |
| #4 Mac paste, clipboard and Unicode | 10 |
| #13 pairing, devices and revoke | 11 |
| #5 omarchy backend and unit | 12, 13 |
| #6 Mac LaunchAgent and install | 14 |
| #7 iOS first version | 15 to 19 |
| #8 double-tap confirm | 20 |
| #9 editor and pages | 21 |
| #10 iCloud sync | 22 |
| #12 iPad layout | 23 |
| #14 TestFlight internal distribution | 24 |
| #2 wrap-up | 25 |

**Rules for whoever builds this:**
- Use American spelling everywhere, in code, comments, commits and docs.
- Each commit ends with `Assisted-by: Claude Code:<model>`. Never add `Co-Authored-By`. Strip any
  `Claude-Session` trailer or claude.ai link.
- Use TDD: write the failing test, run it and watch it fail, then implement and see it pass.
- Commit after every task. Never force-push.
- Issues labeled `hitl-acceptance` end with Eric's on-device check. Leave that box for him.

---

## Part A: the `phrased` helper

### Task 1: Go module skeleton

**Issue:** #3

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

**Issue:** #3

The heart of the helper. It's pure, so it's easy to test. The rules:
- `auto` pastes any text that contains a newline, and anything over 300 runes; it types the rest.
- `type` always types, and newlines become Shift+Enter. Shift+Enter fails in older Alacritty, tmux
  and gnome-terminal, so only explicit `type` mode uses it. The app always sends `auto`.
- `paste` always pastes, newlines included.
- CRLF becomes LF first. The final Enter comes only from `enter`.
- Typed text is chunked by rune, 20 per chunk. Backends may re-chunk (the Mac does, by UTF-16).

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

func check(t *testing.T, got, want []Step) {
	t.Helper()
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %#v want %#v", got, want)
	}
}

func TestTypeSimpleWithEnter(t *testing.T) {
	check(t, Build("hello", true, ModeAuto), []Step{{Kind: TypeText, Text: "hello"}, {Kind: Enter}})
}

func TestNoEnter(t *testing.T) {
	check(t, Build("merge ", false, ModeAuto), []Step{{Kind: TypeText, Text: "merge "}})
}

func TestAutoPastesAnyNewline(t *testing.T) {
	check(t, Build("a\nb", true, ModeAuto), []Step{{Kind: PasteText, Text: "a\nb"}, {Kind: Enter}})
}

func TestAutoNormalizesCRLFBeforePaste(t *testing.T) {
	check(t, Build("a\r\nb", false, ModeAuto), []Step{{Kind: PasteText, Text: "a\nb"}})
}

func TestAutoPastesLongText(t *testing.T) {
	long := strings.Repeat("x", PasteThreshold+1)
	check(t, Build(long, true, ModeAuto), []Step{{Kind: PasteText, Text: long}, {Kind: Enter}})
}

func TestAutoTypesAtThreshold(t *testing.T) {
	got := Build(strings.Repeat("x", PasteThreshold), false, ModeAuto)
	if got[0].Kind != TypeText {
		t.Fatalf("pasted at threshold: %#v", got[0])
	}
}

func TestTypeModeNewlinesBecomeShiftEnter(t *testing.T) {
	check(t, Build("a\nb", true, ModeType), []Step{
		{Kind: TypeText, Text: "a"}, {Kind: ShiftEnter},
		{Kind: TypeText, Text: "b"}, {Kind: Enter},
	})
}

func TestTypeModeCRLF(t *testing.T) {
	check(t, Build("a\r\nb", false, ModeType), []Step{
		{Kind: TypeText, Text: "a"}, {Kind: ShiftEnter}, {Kind: TypeText, Text: "b"},
	})
}

func TestTypeModeNeverPastes(t *testing.T) {
	for _, s := range Build(strings.Repeat("x", 1000), false, ModeType) {
		if s.Kind == PasteText {
			t.Fatal("type mode pasted")
		}
	}
}

func TestChunksAt20Runes(t *testing.T) {
	got := Build(strings.Repeat("é", 45), false, ModeType)
	if len(got) != 3 || len([]rune(got[0].Text)) != 20 || len([]rune(got[2].Text)) != 5 {
		t.Fatalf("bad chunks: %#v", got)
	}
}

func TestForcedPasteKeepsNewlines(t *testing.T) {
	check(t, Build("a\nb", false, ModePaste), []Step{{Kind: PasteText, Text: "a\nb"}})
}

func TestEmptyTextOnlyEnter(t *testing.T) {
	check(t, Build("", true, ModeAuto), []Step{{Kind: Enter}})
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

// ChunkSize is how many runes go into one typing step. Backends may split further.
const ChunkSize = 20

type Step struct {
	Kind Kind
	Text string
}

func Build(text string, enter bool, mode Mode) []Step {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	var steps []Step
	paste := mode == ModePaste || (mode == ModeAuto &&
		(strings.Contains(text, "\n") || len([]rune(text)) > PasteThreshold))
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

**Step 4:** Run `go test ./internal/plan/ -v`. Expected: all tests PASS.

**Step 5:** Commit: `helper: plan phrases into typing steps` (add the new files first).

### Task 3: the `typer` interface and a fake

**Issue:** #3

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
	if err := Run(f, plan.Build("a\nb", true, plan.ModeType)); err != nil {
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
	if err := Run(f, plan.Build("a", true, plan.ModeType)); !errors.Is(err, ErrFake) {
		t.Fatalf("want ErrFake, got %v", err)
	}
}
```

**Step 2:** Run `go test ./internal/typer/`. Expected: FAIL, `undefined: Run`.

**Step 3: Implement.** `typer.go`:

```go
// Package typer sends keystrokes to the focused window.
package typer

import (
	"errors"

	"github.com/ericdahl-dev/phrasedeck/helper/internal/plan"
)

// ErrPasteUnsupported is returned by a backend that can't paste yet. The server maps it to 501.
var ErrPasteUnsupported = errors.New("paste not supported by this backend yet")

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

import (
	"errors"
	"sync"
)

var ErrFake = errors.New("fake failure")

// Fake records calls; used in tests. If Hold is set, Type blocks until it's closed,
// which lets server tests keep the typing lock busy.
type Fake struct {
	mu       sync.Mutex
	Log      []string
	FailOn   string
	NotReady bool
	Hold     chan struct{}
}

func (f *Fake) rec(s string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.FailOn != "" && s == f.FailOn {
		return ErrFake
	}
	f.Log = append(f.Log, s)
	return nil
}

func (f *Fake) Type(t string) error {
	if f.Hold != nil {
		<-f.Hold
	}
	return f.rec("type:" + t)
}
func (f *Fake) Paste(t string) error { return f.rec("paste:" + t) }
func (f *Fake) ShiftEnter() error    { return f.rec("shift-enter") }
func (f *Fake) Enter() error         { return f.rec("enter") }
func (f *Fake) Ready() bool          { return !f.NotReady }
```

**Step 4:** Run `go test ./internal/typer/ -v`. Expected: PASS.

**Step 5:** Commit: `helper: typer interface and fake`.

### Task 4: the token store (check only)

**Issue:** #3

In #3 there's no pairing yet. Eric presets a token by hand: he picks a random token and puts its
SHA-256 in the token file (the README shows how, with `openssl rand -hex 32` and `shasum -a 256`).
Pairing, devices, revoke and file reload come in Task 11 (#13) and extend this store.

**Files:**
- Create: `helper/internal/auth/store.go`
- Test: `helper/internal/auth/store_test.go`

**Behavior:**
- `NewStore(path)` loads the file, or starts empty if it doesn't exist. A file with group or other
  permission bits set is refused with an error that says to `chmod 600` it.
- The JSON shape is `{"devices":[{"name":"iPhone","sha256":"…","paired_at":"…"}]}`.
- `Check(token)` hashes the token with SHA-256 and compares it against every stored hash with
  `subtle.ConstantTimeCompare`, without returning early.

**Step 1: Failing tests:** a preset hash checks true and anything else false; a missing file is an
empty store; a 0644 file is refused; an empty token never checks true. Build the file in the test
with `os.WriteFile(p, data, 0o600)`.

**Step 2:** Run it. Expected: FAIL, `undefined: NewStore`.

**Step 3: Implement** with `crypto/sha256`, `encoding/hex`, `crypto/subtle` and a `sync.Mutex`.

**Step 4:** Run `go test ./internal/auth/ -v`. Expected: PASS.

**Step 5:** Commit: `helper: token store`.

### Task 5: the tailnet filter and Host allowlist

**Issue:** #3

The helper listens on all addresses, but only Tailscale peers get through.

**Files:**
- Create: `helper/internal/tailnet/tailnet.go`
- Test: `helper/internal/tailnet/tailnet_test.go`

**Behavior:**
- `Allowed(ip net.IP) bool` is true only inside `100.64.0.0/10` and `fd7a:115c:a1e0::/48`.
  IPv4-mapped IPv6 addresses are unwrapped first.
- `Listen(addr string) (net.Listener, error)` wraps `net.Listen("tcp", addr)`. Its `Accept` closes
  and skips any connection whose remote address isn't `Allowed`, and logs it once per address.
- `ParseSelf(statusJSON []byte) (names []string, err error)` reads `Self.HostName`, `Self.DNSName`
  (trailing dot removed) and `Self.TailscaleIPs` from `tailscale status --json`.
- `SelfNames(tailscalePath string)` runs `tailscale status --json` with `exec.Command` and calls
  `ParseSelf`. On the Mac the CLI may live inside the Tailscale app, so main takes a `-tailscale`
  flag for its path.
- `HostAllowed(host string, allow []string) bool` strips the port, lowercases, and matches exactly.

**Step 1: Failing tests:**
- A table for `Allowed`: `100.64.0.1` and `100.127.255.254` and `fd7a:115c:a1e0::1` true;
  `100.63.255.255`, `100.128.0.0`, `127.0.0.1`, `192.168.1.5`, `10.0.0.1`, `8.8.8.8`, `::1` and
  `fe80::1` false.
- `Listen`: dial it from `127.0.0.1` and check the connection is closed without a response.
- `ParseSelf` against a small fixture JSON.
- `HostAllowed`: `omarchy-laptop:47811`, `omarchy-laptop.tail1234.ts.net` and `100.101.102.103:47811`
  pass; `evil.example`, `localhost` and an empty host fail.

**Steps 2 to 4:** watch them fail, implement, watch them pass.

**Step 5:** Commit: `helper: tailnet-only listener and Host allowlist`.

### Task 6: the HTTP server

**Issue:** #3

**Files:**
- Create: `helper/internal/server/server.go`
- Test: `helper/internal/server/server_test.go`

**Middleware order:** Host allowlist (403) → method → `Content-Type: application/json` on POST
(415) → `http.MaxBytesReader` at 64 KB (413) → bearer auth (401) → `/type` rate limit (429) →
decode and validate → typing lock.

**Endpoints:**
- `GET /health` takes no auth and returns `{"hostname","os","accessibility_ok"}`. It holds nothing
  secret and lets the app show the status dot before pairing.
- `POST /type` needs `Authorization: Bearer <token>`. It takes `{"text","enter","mode"}`, where an
  empty mode means `auto`. It returns 200 `{"ok":true}`, or:
  - 400 for bad JSON or a bad mode; 401 without a valid token; 403 for a Host not on the allowlist;
  - 409 when the typing lock stays busy for 1 second;
  - 413 for text over 10,000 runes or a body over 64 KB; 415 for the wrong content type;
  - 429 when rate-limited; 501 when the backend can't paste yet (`ErrPasteUnsupported`, Mac until
    #4); 503 when the backend isn't ready; 500 on any other typer error.
- **Rate limit:** a token bucket of 10 per second with a burst of 10, shared across callers,
  applied after auth so bad tokens don't drain it.
- **Typing lock:** a one-slot channel. A request waits up to `Config.LockWait` (1 second by default)
  for it, then answers 409.

**Step 1: Failing tests** use `httptest` with `typer.Fake` and a temp `auth.Store` holding one
preset token. Set `Config.AllowHosts` to the test server's host.

```go
func TestHealthNoAuth(t *testing.T)         // 200, accessibility_ok true
func TestTypeNeedsAuth(t *testing.T)        // no header -> 401, fake.Log empty
func TestTypeOK(t *testing.T)               // {"text":"hi","enter":true} -> 200, Log = [type:hi enter]
func TestTypeMultilineAutoPastes(t *testing.T) // "a\nb" -> Log = [paste:a\nb]
func TestTypeBadMode(t *testing.T)          // mode "x" -> 400
func TestTypeTooLong(t *testing.T)          // 10001 runes -> 413
func TestBodyCap(t *testing.T)              // 65 KB body -> 413
func TestContentType(t *testing.T)          // text/plain -> 415
func TestHostAllowlist(t *testing.T)        // Host: evil.example -> 403, Log empty
func TestTypeNotReady(t *testing.T)         // Fake.NotReady -> 503, and /health accessibility_ok=false
func TestRateLimitAfterAuth(t *testing.T)   // 20 bad-token requests then 1 good one -> 401s, then 200
func TestRateLimit(t *testing.T)            // 11 rapid good requests -> at least one 429
func TestBusyLock409(t *testing.T)          // Fake.Hold open, LockWait 50ms: first request blocks, second -> 409
func TestPasteUnsupported501(t *testing.T)  // backend returns ErrPasteUnsupported -> 501
```

Write each test in full. Build JSON with `bytes.NewBufferString`.

**Step 2:** Run them. Expected: FAIL.

**Step 3: Implement** `server.New(cfg Config) http.Handler`, where
`Config{Store *auth.Store; Typer typer.Backend; Hostname, OS string; AllowHosts []string; LockWait time.Duration}`.
Use `golang.org/x/time/rate` (run `go get golang.org/x/time/rate`). Pairing routes are added in
Task 11.

**Step 4:** Run `go test ./... -v`. Expected: PASS.

**Step 5:** Commit: `helper: http server with auth, limits and hardening`.

### Task 7: the macOS typer backend (typing)

**Issue:** #3

**Files:**
- Create: `helper/internal/typer/darwin.go` (`//go:build darwin`)

**Implementation notes:**
- Use cgo with `#cgo LDFLAGS: -framework ApplicationServices -framework AppKit`.
- **Type(text):** convert to UTF-16, call `CGEventCreateKeyboardEvent(nil, 0, true)`, then
  `CGEventKeyboardSetUnicodeString(ev, n, buf)`, `CGEventPost(kCGHIDEventTap, ev)`, the same for
  key-up, and `CFRelease`. Sleep 2 ms between chunks. (Task 10 makes the chunking UTF-16 safe.)
- **Enter:** virtual keycode 36 (`kVK_Return`), down then up. **ShiftEnter:** the same key with
  `CGEventSetFlags(ev, kCGEventFlagMaskShift)`.
- **Paste:** returns `typer.ErrPasteUnsupported` until Task 10.
- **Ready:** `CGPreflightPostEventAccess()`. It checks the right permission for posting events and
  never prompts.

**Test:** a build-only check, `GOOS=darwin go vet ./...`. Typing is checked by hand in Task 8.

**Commit:** `helper: macOS CGEvent typer`.

### Task 8: wire up `main`, plus a signed build

**Issue:** #3

**Files:**
- Modify: `helper/cmd/phrased/main.go`
- Create: `helper/Makefile`
- Create: `README.md` at the repo root (what it is, and the helper section)

**Behavior:**
- `phrased` with no subcommand runs the server. Flags: `-port 47811`, `-config ~/.config/phrased`,
  `-tailscale <path>` and a repeatable `-allow-host <name>`.
- Steps: pick the backend by `runtime.GOOS`, load the store, read the allowlist with
  `tailnet.SelfNames` (and add `-allow-host` names), then serve on `tailnet.Listen(":47811")` with an
  `http.Server` that sets `ReadHeaderTimeout: 5 * time.Second`, `ReadTimeout`, `WriteTimeout` and
  `IdleTimeout`. Log `listening on :47811 (tailnet peers only)` and the allowlist.
- If `tailscale status` fails, log why and exit non-zero: without an allowlist every request would
  get 403.
- Shut down cleanly on SIGINT and SIGTERM.
- **Makefile:** `build` runs `go build` and then, on macOS, signs the binary with a stable
  identity: `codesign --force --sign "$(SIGN_IDENTITY)" --identifier dev.ericdahl.phrased`.
  `SIGN_IDENTITY` defaults to Eric's Apple Development certificate and can be overridden. A stable
  signature keeps the Accessibility grant across rebuilds. `test` runs `go test ./...`.

**README, helper section:** how to build, grant Accessibility, preset a token by hand, and type
with curl.

**Test:** run `go build ./... && go vet ./... && go test ./...`, all clean.

**By-hand check (Eric, on the Mac; record the results in the PR):**
1. Run `make build` and start `./phrased`. Grant Accessibility to it once.
2. From another tailnet device (or the Mac's own tailnet IP), `curl http://<tailnet-name>:47811/health`
   returns `accessibility_ok: true`. A request with a Host header that isn't on the allowlist gets
   403.
3. Rebuild with `make build` and check that Accessibility is still granted.
4. With the preset token, `POST /type` `{"text":"what is the status of the agents? ask them if needed","enter":true}`
   lands in a Claude Code terminal and sends. (Single-line only: multi-line pastes arrive in #4.)

**Commit:** `helper: main wiring and signed build`.

### Task 9: CI

**Issue:** #3

**Files:**
- Create: `.github/workflows/helper.yml`. On a push or PR, `ubuntu-latest` runs `go test -race ./...`,
  `go vet ./...` and `gofmt -l` (failing if it lists anything), and `macos-latest` runs
  `go build ./...` and `go vet ./...`. There's no headless Wayland test; Linux typing is checked by
  hand in #5.
- Make sure the org's required American spelling workflow passes. If the repo doesn't pick it up on
  its own, copy its caller from another ericdahl-dev repo.

**Commit:** `ci: helper tests on linux and macos build`.

### Task 10: Mac Unicode chunking and paste with clipboard restore

**Issue:** #4

**Files:**
- Modify: `helper/internal/typer/darwin.go`
- Create: `helper/internal/typer/utf16chunk.go` (no build tag, so CI tests it on Linux)
- Test: `helper/internal/typer/utf16chunk_test.go`

**Behavior:**
- `utf16Chunks(s string, max int) [][]uint16` splits into chunks of at most `max` UTF-16 units and
  never splits a surrogate pair. `Type` re-chunks the planner's text with `max = 20`.
- **Paste(text):** through cgo and `NSPasteboard`:
  1. Save every pasteboard item, with the data for each of its types (not just plain text).
  2. Clear the pasteboard, set the text as a string, and record `changeCount`.
  3. Send Cmd+V (keycode 9 with `kCGEventFlagMaskCommand`).
  4. Wait at least 500 ms.
  5. Restore the saved items only if `changeCount` still equals the recorded value. If it moved,
     Eric copied something new in the meantime, so leave it.

**Step 1: Failing tests** for `utf16Chunks`: ASCII splits at 20; 19 ASCII then "😀" puts the emoji
whole in the second chunk; 45 "é" gives 20, 20 and 5; every chunk is 20 or fewer units; joining
the chunks gives back the input.

**Steps 2 to 4:** watch them fail, implement, watch them pass.

**By-hand check (Eric, on the Mac):** "naïve café ✓" and a phrase with emoji type correctly into
TextEdit and a Claude Code terminal; a multi-line phrase pastes as one block in both; a copied
image is still on the clipboard afterward; and copying something during the 500 ms keeps the new
copy.

**Commit:** `helper: macOS UTF-16 chunking and paste with clipboard restore`.

### Task 11: pairing, devices and revoke

**Issue:** #13

**Files:**
- Create: `helper/internal/auth/pairing.go`
- Modify: `helper/internal/auth/store.go`
- Modify: `helper/internal/server/server.go`
- Modify: `helper/cmd/phrased/main.go`
- Test: `helper/internal/auth/pairing_test.go`, more cases in `store_test.go` and `server_test.go`

**How `phrased pair` reaches the running helper:** through a file, the same way revoke works.
`phrased pair` generates a 6-digit code with `crypto/rand`, writes `pairing.json` (mode 0600, via a
temp file and a rename) holding a new random window ID, the code's SHA-256 and an expiry 2 minutes
out, prints the code, and on the Mac shows a notification with `osascript` (the code passed as an
argument to a fixed script with `on run argv`, never spliced into it). The running helper reads
`pairing.json` when `/pair` is called.

**Behavior:**
- `POST /pair` with `{"code","device"}`:
  - 404 when there's no `pairing.json`, it has expired, its window was already used, or pairing is
    locked.
  - 401 for a wrong code. Failures are counted in total, not per window. After 10, pairing is locked
    until a `pairing.json` with a new window ID appears, which resets the count.
  - 200 `{"token"}` for the right code: a random 32-byte token as hex. Only its SHA-256 is saved.
    The window closes (the helper deletes `pairing.json`), so a code is single-use.
  - It has its own limiter (1 per second, burst 3), separate from `/type`. Codes compare in
    constant time.
- `Store.Add(name) (token string, err error)`: a duplicate name gets a suffix, " (2)" and so on.
- **Reload:** before each check, the store stats the token file and reloads it if its modification
  time or size changed. Writes go through a temp file and a rename, with mode 0600.
- `phrased devices` prints each device's name and paired date. `phrased revoke <name>` removes it
  and saves. The running helper sees both on its next request.

**Step 1: Failing tests:**
- No window → 404. A window, wrong code → 401; right code → token that works on `/type`; the same
  code again → 404.
- An expired window → 404 (inject `now`).
- 10 wrong codes → the right code now gets 404; a new window ID → the right code works.
- The `/pair` limiter returns 429 without touching the `/type` bucket.
- No plain token or plain code ever reaches the token file or `pairing.json`.
- Revoke from a second `Store` on the same path → the first store's `Check` fails on its next call.
- Adding "iPhone" twice gives "iPhone" and "iPhone (2)".

**Steps 2 to 4:** watch them fail, implement, watch them pass.

**README:** replace "preset a token by hand" with pairing: run `phrased pair`, enter the code.

**By-hand check (Eric, on the Mac):** `phrased pair` prints a code and shows a notification; curl
`POST /pair` with it returns a token that types; `phrased devices` lists the device; after
`phrased revoke` the token gets 401 without restarting the helper.

**Commit:** `helper: phrased pair, devices and revoke`.

### Task 12: the Linux typer backend

**Issue:** #5

**Files:**
- Create: `helper/internal/typer/linux.go` (`//go:build linux`)
- Test: `helper/internal/typer/linux_test.go` (`//go:build linux`)

**Implementation notes:**
- Build every command with `exec.Command` and argument slices, never `sh -c`.
- **Never capture stdout or stderr from a process that forks** (such as a background `wl-copy`):
  the forked child holds the pipe open and the read never ends. Leave `Stdout` and `Stderr` nil for
  those. Only capture output from `wl-paste`, which doesn't fork.
- **Type(text):** `wtype -- <text>`.
- **Enter:** `wtype -k Return`. **ShiftEnter:** `wtype -M shift -k Return -m shift`.
- **Paste(text):**
  1. Save the clipboard with `wl-paste --no-newline` (allow it to fail; plain text only).
  2. Start `wl-copy --foreground --paste-once` with the text on stdin. It serves one paste, then
     exits.
  3. Send Ctrl+Shift+V: `wtype -M ctrl -M shift -k v -m shift -m ctrl`.
  4. Wait for `wl-copy` to exit, with a 2-second cap (kill it if it's still there), instead of
     sleeping a fixed time.
  5. Restore with `wl-copy` (text on stdin, output not captured), or `wl-copy --clear` if the saved
     clipboard was empty.
- **Ready:** `exec.LookPath("wtype")` succeeds and `WAYLAND_DISPLAY` is set.
- Make the runner injectable, so the test can record argument lists and stdin.

**Step 1: Failing test:** with the runner swapped out, check that `Type("a b")` runs
`["wtype","--","a b"]`, that text starting with `-` stays after `--`, that `ShiftEnter` has the
expected arguments, and that `Paste` runs `wl-copy --foreground --paste-once` with the text on
stdin, then the key chord, then waits, then restores.
**Steps 2 to 4:** watch it fail, implement it, watch it pass (CI runs it on Linux).
**Commit:** `helper: Linux wtype typer`.

### Task 13: the systemd user unit and Linux install

**Issue:** #5

**Files:**
- Create: `helper/install/phrased.service`
- Modify: `helper/Makefile` (add `install-linux`)
- Modify: `README.md` (omarchy section)

**The unit:**
- `PartOf=graphical-session.target`, `After=graphical-session.target`, and
  `WantedBy=graphical-session.target`.
- No hardcoded `WAYLAND_DISPLAY`. It comes from the user manager's environment, which uwsm (and the
  graphical session) imports. `ConditionEnvironment=WAYLAND_DISPLAY` stops the unit from starting
  without it.
- `ExecStart=%h/.local/bin/phrased` and `Restart=on-failure`.

`install-linux` builds, copies the binary to `~/.local/bin`, installs the unit to
`~/.config/systemd/user/`, and runs `systemctl --user daemon-reload` and
`systemctl --user enable --now phrased`.

**By-hand check (Eric, on omarchy):** run `pacman -S wtype wl-clipboard` and `make install-linux`;
after logging in again, `systemctl --user status phrased` is active; `/health` answers on the
tailnet name; pair with `phrased pair` and curl; "naïve café ✓" types and a multi-line phrase
pastes into a terminal running Claude Code, and the clipboard comes back afterward.

**Commit:** `helper: systemd user unit and Linux install`.

### Task 14: the Mac LaunchAgent and install

**Issue:** #6

**Files:**
- Create: `helper/install/dev.ericdahl.phrased.plist` (a LaunchAgent with RunAtLoad and KeepAlive,
  running `~/.local/bin/phrased`, with logs in `~/Library/Logs/phrased.log`)
- Modify: `helper/Makefile` (add `install-mac`)
- Modify: `README.md` (Mac install section)

`install-mac` reuses the signed `build` from Task 8 (so the Accessibility grant carries over),
copies the binary to `~/.local/bin`, writes the plist with the home path filled in, and runs
`launchctl bootout` (ignoring "not loaded") then `launchctl bootstrap gui/$(id -u)` on it.

**By-hand check (Eric, on the Mac):** after logging in again, `/health` answers on the tailnet
name with `accessibility_ok: true`, and the log file is being written.

**Commit:** `helper: Mac LaunchAgent and install target`.

---

## Part B: the iOS app

Set the bundle ID to `dev.ericdahl.phrasedeck`, with iOS 18 or later, iPhone and iPad. During
development, run straight from Xcode on Eric's devices. Installs go through internal TestFlight
(Task 24).

### Task 15: project skeleton (XcodeGen)

**Issue:** #7

**Files:**
- Create: `ios/project.yml` with three targets:
  - `Phrasedeck`, the app.
  - `PhrasedeckTests`, unit tests with `Phrasedeck` as the **test host**, because Keychain calls
    need an app's entitlements to work in the simulator.
  - `PhrasedeckUITests`, a UI test target.
- Info.plist settings:
  - `NSAppTransportSecurity`: an `NSExceptionDomains` entry for `ts.net` with
    `NSIncludesSubdomains` and `NSExceptionAllowsInsecureHTTPLoads`, plus `NSAllowsLocalNetworking`
    for bare tailnet names and IPs, where it applies. The helper is plain HTTP inside the tailnet.
  - `ITSAppUsesNonExemptEncryption = NO`. The app uses no encryption beyond what the OS provides, so
    this answers export compliance for TestFlight up front.
  - No `NSBonjourServices` (Bonjour is deferred). If iOS asks for local-network access when
    reaching a tailnet IP, add `NSLocalNetworkUsageDescription` then.
- **No iCloud capability or entitlement.** Task 22 (#10) adds it.
- Create: `ios/Phrasedeck/PhrasedeckApp.swift`, with a `ModelContainer` whose configuration uses
  `cloudKitDatabase: .none`, and `ContentView.swift` (placeholder text).
- Add `ios/*.xcodeproj` to `.gitignore`; the project file is generated.

**Check:** `cd ios && xcodegen && xcodebuild -scheme Phrasedeck -destination 'generic/platform=iOS Simulator' build`
succeeds, and `xcodebuild test` runs the empty test targets. **Commit:** `ios: XcodeGen skeleton`.

### Task 16: the data model and starter phrases

**Issue:** #7

**Files:**
- Create: `ios/Phrasedeck/Model.swift`
- Test: `ios/PhrasedeckTests/ModelTests.swift`

**Model:** SwiftData `@Model`s, with every property defaulted or optional, so the CloudKit switch
in Task 22 needs no migration.
- `Phrase(label, text, pressEnter, confirm, colorName, order, page, seedKey: String?)`
- `Page(name, order, phrases, seedKey: String?)`

`Helper` is **not** a SwiftData model; it lives in Task 17's local store.

`Seed.insertMissing(context)` adds the "Agents" page and the five starter phrases from the
design's table. Each has a fixed `seedKey` (`page.agents`, `phrase.agent-status` and so on), and
seeding inserts only the keys that aren't there yet. It runs once per device (a `UserDefaults`
flag), so a starter phrase Eric deletes stays deleted.

**TDD:** first write the tests, using an in-memory `ModelContainer`: seeding twice adds nothing the
second time; after the flag is set, a deleted starter phrase isn't re-added; the starter phrases have the right Enter and confirm flags; and phrases sort by `order`.
Watch them fail, implement, watch them pass. **Commit:** `ios: model and starter phrases`.

### Task 17: the local computers store and the Keychain token store

**Issue:** #7

**Files:**
- Create: `ios/Phrasedeck/HelperStore.swift`, `ios/Phrasedeck/TokenStore.swift`
- Test: `ios/PhrasedeckTests/HelperStoreTests.swift`, `ios/PhrasedeckTests/TokenStoreTests.swift`

**HelperStore:** `Helper { id: UUID, name, host, port }` as `Codable`, saved as JSON in
`UserDefaults` (a local-only SwiftData store would also be fine; UserDefaults is simpler and can't
sync by accident). It also holds `selectedHelperID`, so each device remembers its own choice.
`delete(helper)` removes it and calls `TokenStore.delete(helper.id)`.

**TokenStore:** `set(token, for helperID)`, `get(helperID)` and `delete(helperID)`, using a
`kSecClassGenericPassword` item with service `dev.ericdahl.phrasedeck`, account `helperID`, and
`kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`, so the token never syncs.

**TDD:** HelperStore round trip, the selected ID surviving a reload, delete clearing the Keychain
item; TokenStore round trip, overwrite replacing the old value, and delete removing it (these run
in the test host). **Commit:** `ios: local computers and keychain tokens`.

### Task 18: `HelperClient`

**Issue:** #7

**Files:**
- Create: `ios/Phrasedeck/HelperClient.swift`
- Test: `ios/PhrasedeckTests/HelperClientTests.swift`, with a `URLProtocol` stub

**API:**
- `health() async throws -> Health`
- `pair(code:device:) async throws -> String`
- `type(text:enter:) async throws`, which **always sends `mode: "auto"`**. There's no mode
  parameter.
- A 3-second timeout, and **no retries**.
- Errors:
  - `.noReply`: a timeout, a lost connection mid-request, or 409. The phrase may have been typed
    (amber in the UI).
  - `.unreachable`: the connection was refused or the host couldn't be resolved
    (`cannotConnectToHost`, `cannotFindHost`, `dnsLookupFailed`) (red).
  - `.unauthorized` (401), `.noPairingWindow` (404 from `/pair`), `.notReady` (503),
    `.rateLimited` (429) and `.server(Int)`.

**TDD:** each status code and URL error maps to the right case; `type` sends the Bearer header,
`Content-Type: application/json` and a body with `mode` `auto`; a timeout comes back as `.noReply`
and a refused connection as `.unreachable`; and each call makes exactly one request, which proves
there are no retries. **Commit:** `ios: helper client`.

### Task 19: the deck, send feedback, adding a computer and pairing

**Issue:** #7

**Files:**
- Create: `ios/Phrasedeck/DeckView.swift`, `PhraseButton.swift`, `HelperPicker.swift`,
  `ComputersView.swift`, `AddHelperView.swift`
- Modify: `ContentView.swift`
- Test: `ios/PhrasedeckUITests/SendTests.swift`

**Behavior:**
- A paged `TabView`, one page per `Page`, each holding a `LazyVGrid` of `PhraseButton`s, 3 columns.
  (The iPad grid is Task 23.)
- A tap fires `UIImpactFeedbackGenerator(.light)` and calls `client.type` once. A green flash means
  success. `.noReply` shows an amber flash and "no reply, it may have typed". `.unreachable` shows
  red and "<name> unreachable". Other errors show red with their own text.
- After `.unauthorized`, the banner offers **Pair again**, which opens the code entry for that
  computer.
- `HelperPicker` shows the computers and a status dot that polls `/health` every 5 seconds while
  the app is in the foreground: green reachable, red unreachable, amber when `accessibility_ok` is
  false.
- **Add a computer:** enter a tailnet name (for example `omarchy-laptop`) and an optional port. The
  screen tells Eric to run `phrased pair` on that computer, then asks for the 6-digit code, calls
  `pair`, and saves the token with `TokenStore`. A 404 says "run phrased pair on <name> first".
- `ComputersView` lists computers with swipe to delete, which clears the Keychain item.

**Tests:** a UI test that starts the app with a launch argument selecting a stub client, and
checks that a tap sends exactly once.

**By-hand check (Eric):** run from Xcode on his iPhone, add the Mac by tailnet name, pair, and tap
"Agent status" into a Claude Code terminal; stop the helper and see red "unreachable"; revoke the
phone on the Mac and see "Pair again".

**Commit:** `ios: deck, send feedback and pairing`.

### Task 20: double-tap confirm

**Issue:** #8

**Files:**
- Modify: `ios/Phrasedeck/PhraseButton.swift`
- Test: `ios/PhrasedeckUITests/ConfirmTests.swift`

**Behavior:** phrases with `confirm` need a double-tap. The first tap shows "tap again", which
cancels after 1.5 seconds; a second tap in that time sends once. Phrases without `confirm` still
send on one tap.

**Tests:** a UI test with the stub client: a confirm phrase sends only on the second tap, a normal
phrase on the first, and a second tap after 1.5 seconds doesn't send.

**Commit:** `ios: double-tap confirm`.

### Task 21: the phrase editor and pages

**Issue:** #9

**Files:**
- Create: `ios/Phrasedeck/PhraseEditor.swift`, `PageEditor.swift`
- Modify: `PhraseButton.swift`, `DeckView.swift`
- Test: more cases in `ModelTests.swift`

**Behavior:**
- Long-press on a button uses `.contextMenu` with Edit and Delete. Edit opens the editor sheet:
  label, multi-line text, the Enter and Confirm toggles, a color picker from 6 named colors, and a
  page picker.
- A "+" tile adds a phrase to the current page.
- Pages can be added, renamed and reordered, and are swiped between in order.

**TDD:** ordering, moving a phrase between pages, and deleting a page's phrases with it.
**Commit:** `ios: phrase editor and pages`.

### Task 22: iCloud sync

**Issue:** #10

**Human step first:** Eric creates the iCloud container `iCloud.dev.ericdahl.phrasedeck` in his
Apple developer account and enables it for the app ID.

**Files:**
- Modify: `ios/project.yml` (add the iCloud capability with CloudKit, the container, and the
  Background Modes remote-notification entitlement)
- Modify: `ios/Phrasedeck/PhrasedeckApp.swift` (switch the configuration to
  `cloudKitDatabase: .private("iCloud.dev.ericdahl.phrasedeck")`)
- Modify: `ios/Phrasedeck/Model.swift` (seed dedupe)

**Behavior:**
- Phrases and pages sync. Computers and tokens don't: they were never in the synced store.
- **Seed dedupe:** both devices seed before sync catches up, so after a remote change arrives,
  rows sharing a `seedKey` are merged, keeping the oldest and moving any phrases onto the kept page.

**TDD:** two seeded in-memory stores merged into one end up with exactly one row per `seedKey`.

**By-hand check (Eric):** a phrase added on the iPhone shows up on the iPad and the other way
round; the deck isn't doubled; each device still pairs on its own.

**Commit:** `ios: iCloud sync`.

### Task 23: iPad layout

**Issue:** #12

**Files:**
- Modify: `ios/Phrasedeck/DeckView.swift`, `PhraseButton.swift`

**Behavior:** the iPhone keeps 3 columns; the iPad uses an adaptive grid with more buttons per
page, in both orientations, with labels still large and every button a comfortable tap size.

**By-hand check (Eric, on his iPad):** both orientations, and split view.

**Commit:** `ios: iPad layout`.

### Task 24: TestFlight internal distribution

**Issue:** #14

**Human step first:** Eric creates the App Store Connect app record for `dev.ericdahl.phrasedeck`
and an internal testing group with only his own account. There are no external testers and no App
Store release, so builds don't go through Beta App Review.

**Files:**
- Create: `ios/ExportOptions.plist` (`method` `app-store-connect`, `destination` `upload`, Eric's
  team ID, automatic signing)
- Create: `ios/scripts/testflight.sh`
- Modify: `README.md` (iOS install section)

**The script:** runs `xcodegen`, sets the build number (`CFBundleVersion`) to a value that always
grows (for example the UTC date and time), runs `xcodebuild archive`, then
`xcodebuild -exportArchive -exportOptionsPlist ios/ExportOptions.plist` with the App Store Connect
API key passed by `-authenticationKeyPath`, `-authenticationKeyID` and
`-authenticationKeyIssuerID`. The key comes from Eric's secret store at run time and is never
committed. Export compliance is already answered by `ITSAppUsesNonExemptEncryption = NO` from
Task 15.

**Prior art:** ShowRecorder's `.github/workflows/release.yml` archives on a tag and uploads with
an API key and an export options file whose destination is `upload`. Its
`scripts/testflight_promote.py` sends builds to external testers, which phrasedeck doesn't need.
Copy no secrets from it.

**By-hand check (Eric):** the uploaded build appears in TestFlight and installs on his iPhone and
iPad.

**Commit:** `ios: TestFlight internal upload`.

### Task 25: wrap up

**Issue:** #2

- Make sure the README covers both helpers, pairing, revoke, the iOS TestFlight install and
  running from Xcode during development.
- Optional hardening note in the README: a Tailscale ACL that lets only Eric's phones reach port
  47811 on the two computers.
- Fill in the design's manual checklist on both machines, Unicode and multi-line included, and
  paste the results into the PR.
- Open a PR from the working branch and ask Eric to review. Don't merge.
