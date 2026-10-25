# phrasedeck design

Date: 2026-10-24 (revised the same day after review)
Status: approved design, not yet built

## Goal

A Stream Deck-style iOS app for Eric's own use. Each button types an often-used phrase into the
focused window on his Mac or on omarchy-laptop. The main target is a Claude Code terminal, for
prompts like "what is the status of the agents? ask them if needed".

Not a product: there's no App Store release and no other users. Builds reach Eric's iPhone and
iPad through internal TestFlight only.

## Pieces

1. **iOS app** (SwiftUI, iPhone and iPad): a grid of phrase buttons, in pages.
2. **`phrased`** (Go): one helper binary for macOS and Linux. It receives a phrase over the network
   and types it into the focused window.

The text always goes to whatever window has focus on the chosen computer. The phone only picks
the computer.

## Network, pairing and protocol

### Tailnet route

- The helper listens on all addresses on port 47811, but drops any connection whose remote
  address is outside Tailscale (`100.64.0.0/10` and `fd7a:115c:a1e0::/48`) right after accept.
  That filter replaces picking addresses at startup.
- Computers are added in the app by scanning the QR code from `phrased pair`, or by tailnet
  name, for example `omarchy-laptop`.
- Bonjour and a LAN route are opt-in; see "Home network (LAN) route" below (#11).
- **Host allowlist** against DNS rebinding: a request whose `Host` header (port stripped) isn't the
  machine's tailnet name, its MagicDNS name or one of its tailnet IPs gets 403. The helper reads
  those from `tailscale status --json` at startup, and a repeatable `-allow-host` flag adds names.
- Optional hardening: a Tailscale ACL that lets only Eric's phones reach port 47811.

### Home network (LAN) route

Decided 2026-10-25 (#11): Tailscale stays the way in at work, and at home phrasedeck also works
with Tailscale off, on macOS and omarchy, over an encrypted LAN route.

- **Opt-in:** `-lan <cidr>` (repeatable) or `<config>/lan-subnets`. With none, nothing changes.
  Only private subnets are accepted.
- **Second listener:** HTTPS (TLS 1.2 or later) on 47812, open to the `-lan` subnets and tailnet
  peers. It serves `/health`, `/pair` and `/type` with the same tokens, limits and lockout as
  47811, which is unchanged. `/info` is tailnet-only (404 on 47812), since it hands out the pin.
- **Certificate:** self-signed ECDSA P-256, made once in the config dir (key 0600) and reused.
  The pin, `certSHA256`, is the lowercase hex SHA-256 of the leaf's DER (64 characters, no
  colons).
- **Host allowlist** per listener: 47811 keeps the tailnet names; 47812 accepts the machine's
  LAN IPs, `<host>.local` and `phrased-<host>.local`. `-allow-host` applies to both.
- **Bonjour:** `_phrasedeck._tcp` on the LAN interfaces, from the pure-Go `brutella/dnssd`, as
  host `phrased-<host>.local`. A separate name means phrased never publishes records for the
  system's own `<host>.local`, which mDNSResponder or avahi owns. TXT is `v=1`,
  `ts=<MagicDNS name, lowercase, no trailing dot>`, `fp=<certSHA256>` and `port=47812`, with no
  secrets. `ts` is left out while Tailscale is off and added live when it comes up. `fp` lets
  the app match a computer paired over the LAN only; it is for matching, never trust, since
  the app trusts only the pin it stored at pairing and the TLS handshake proves it.
- **Wire fields:** `/pair` 200 is `{"token","certSHA256","lanPort"}`, with the last two only when
  LAN is on. `GET /info` (Bearer) is `{"certSHA256","lanPort","hostname"}`. The QR code adds
  `fp=<64 hex>&lan=<IP literal>&lanPort=<port>`, all or none, and `v` stays 1. `host` stays
  tailnet-only: with Tailscale off the QR leaves out `host` and `port`.
- **App:** browses `_phrasedeck._tcp` and calls a paired computer "nearby" when its `ts` matches.
  It sends over `https://<lan ip>:47812` with the certificate pinned, and fails closed on a
  mismatch, never falling back silently. The pin comes only from `/pair` over the tailnet, the
  QR code or `/info`, never from Bonjour.

### Pairing

- Pairing starts on the computer, with the local command **`phrased pair`**. It opens a 2-minute
  pairing window and prints a 6-digit code in the terminal; on the Mac it also shows a
  notification. The command writes the window (the code's hash and its expiry) to
  `~/.config/phrased/pairing.json` (mode 0600), and the running helper picks it up.
- **QR code (#25):** under the text code, `phrased pair` prints a QR code for
  `phrasedeck://pair?host=<host>&port=<port>&code=<6 digits>&v=1`. The host is the machine's
  MagicDNS name from `tailscale status --json` (its tailnet IP when MagicDNS is off). The port is
  the one the running helper recorded in `~/.config/phrased/port` at startup (47811 otherwise).
  Without Tailscale it prints only the text code, unless the LAN route is on (see above).
- **In the app**, "Add a computer" leads with **Scan QR code**, and typing the name and code still
  works. The app registers the `phrasedeck` URL scheme, so scanning with the Camera app opens the
  same flow. A link is checked before anything is contacted: the host must be a `*.ts.net` name
  or a Tailscale address (`100.64.0.0/10`, `fd7a:115c:a1e0::/48`), the code 6 digits, and `v=1`.
  A good link pairs after one confirm tap that shows the host. A computer that's already in the
  list is paired again rather than added twice. A scanned code is the same code as a typed one,
  so the window, single use and lockout below all apply.
- `POST /pair` with `{code, device}` returns `{token}` while a window is open, 401 for a wrong
  code, and **404 when no window is open**. A code is single-use: success closes the window.
- **Lockout:** after about 10 failed attempts in total, `/pair` stays locked (404) until
  `phrased pair` runs again. `/pair` has its own rate limiter, separate from `/type`.
- The phone stores the long-lived token in its Keychain. The helper keeps only token hashes, in
  `~/.config/phrased/tokens.json` (mode 0600), and reloads that file when it changes.
- **Devices and revoke:** `phrased devices` lists paired devices, and `phrased revoke <name>`
  removes one. Both edit the token file, and the running helper sees the change on its next
  request.

### HTTP API

Plain HTTP with a bearer token, inside the tailnet.

- `POST /type` with `{text, enter: bool, mode: "auto"|"type"|"paste"}` returns 200 after the text
  has been typed. The app always sends `mode: "auto"`; `type` and `paste` are for curl and tests.
- `GET /health` returns `{hostname, os, accessibility_ok}` with no auth.
- `POST /pair` as above.

### What each status means (#44)

The helper sets these in one table (`server/answers.go`), and the app reads only the code, showing
the `{"error": "..."}` text when there is one. Every refusal has that body.

| Code | Meaning | Typed? | App shows |
|---|---|---|---|
| 200 | typed | yes | green flash |
| 400 | bad JSON, mode or device name | no | red |
| 401 | bad token, or a wrong pairing code | no | red, **Pair again** |
| 403 | `Host` not allowed | no | red |
| 404 | no pairing window open, or pairing locked | no | red |
| 409 | busy: another phrase held the keyboard for the whole 1-second wait | **no**, tap again | "<computer> is busy, tap again"; never retried |
| 413 | body or text too large | no | red |
| 415 | not `application/json` | no | red |
| 429 | rate-limited | no | red |
| 500 | typing failed partway | **maybe** | amber, "it may have typed" |
| 501 | this computer can't paste | no | red |
| 503 | can't type yet (Accessibility not granted) | no | red |

A send that times out or drops after the request went out is also "it may have typed".

### Server limits

- `/type` is rate-limited to 10 requests per second, applied after auth, so a stuck button can't
  flood the keyboard.
- Typing is serialized by a lock. A request waits at most 1 second for it, then gets **409**.
- `ReadHeaderTimeout` is set, request bodies are capped at 64 KB, and `POST` bodies must be
  `application/json` (415 otherwise).

## iOS app

### Data

```
Phrase { id, label, text, pressEnter: Bool, confirm: Bool, color, page, order, seedKey? }
Page   { id, name, order, seedKey? }
Helper { id, name, host, port }   // local to each device; token in the Keychain, keyed by Helper.id
```

- Phrases and pages are stored in SwiftData. They start local only (`cloudKitDatabase: .none`,
  and no iCloud entitlement). The iCloud sync slice (#10) adds the CloudKit container and the
  entitlement, and then they sync between the iPhone and iPad.
- **Computers stay local to each device.** `Helper` records live in UserDefaults (a local-only
  SwiftData store would also do), never in the synced store. The stable `Helper.id` is the
  Keychain key. The selected computer is stored per device too.
- Tokens never sync, so each device pairs on its own.

### Screens

- **Main screen:** a computer picker at the top with a status dot. Green means reachable, red
  means unreachable, and amber means the Mac lacks Accessibility permission. Below it is a grid of
  buttons, one page per swipe: 3×4 on an iPhone, more on an iPad. Each button shows its label in
  large type, with a ⏎ badge if it presses Enter.
- **Tap** sends the phrase. Phrases marked `confirm` need a double-tap.
- **Long-press** (a `.contextMenu`) edits a button: label, text, Enter, confirm, color and page, or
  delete it. A "+" tile adds a phrase.
- **Computers list:** swipe to delete a computer, which also clears its Keychain item. After a
  401, the banner offers **Pair again**.
- **Feedback:** a haptic tick on a tap and a green flash when the helper returns 200. Failures show
  a flash plus a banner, in two kinds:
  - **Amber, "it may have typed":** the send timed out or dropped after it went out, or the
    helper answered 500. A 409 means nothing was typed (see "What each status means").
  - **Red, "unreachable":** the connection was refused, or the host name couldn't be resolved.
  - Other errors (unauthorized, not ready, rate-limited) are red with their own text.
- **No automatic retry.** A retry could type the phrase twice.
- A send times out after 3 seconds.

### Networking

- App Transport Security allows plain HTTP to `*.ts.net` (an exception domain that includes
  subdomains) and to bare tailnet names and IPs (`NSAllowsLocalNetworking` where it applies).

### Starter phrases

| Label | Text | Enter | Confirm |
|---|---|---|---|
| Agent status | what is the status of the agents? ask them if needed | yes | no |
| Status table | what is the status of the agents? ask them if needed.  show me in a table | yes | no |
| Merge… | `merge ` | no | no |
| Yes | yes | yes | yes |
| Merge it | Merge it | yes | yes |

Each seeded row carries a fixed `seedKey`. Seeding runs once per device and inserts only the keys
that are missing, and
once sync is on, rows that share a `seedKey` are merged, so two devices don't double the deck.

## `phrased` helper (Go)

- `server`: `net/http` behind the tailnet filter, the Host allowlist, a constant-time token check,
  the limits above, and the 1-second typing lock.
- `plan`: turns `(text, enter, mode)` into steps.
  - **`auto` mode:** paste any text that contains a newline, and anything over 300 characters;
    type the rest. Claude Code collapses long pastes into "[Pasted text]", so short single-line
    phrases are typed.
  - **`type` mode:** newlines are typed as Shift+Enter, so a multi-line phrase doesn't send early.
    This happens only in explicit `type` mode, because Shift+Enter fails in older Alacritty, tmux
    and gnome-terminal.
  - **`paste` mode** always pastes, newlines included.
  - The final Enter comes only from the `enter` flag. CRLF is normalized to LF.
  - Typed text is chunked by rune (20 per chunk); a backend may re-chunk.
- `typer` interface: `Type(text)`, `Paste(text)`, `ShiftEnter()`, `Enter()`, `Ready()`.
  - **macOS:** CGEvent with `CGEventKeyboardSetUnicodeString`, through cgo. The backend re-chunks
    into 20 or fewer UTF-16 units and never splits a surrogate pair. Paste saves every pasteboard
    item and type, sets the text, sends Cmd+V, and restores after 500 ms or more, but only if the
    pasteboard `changeCount` hasn't moved since the paste. Ready uses `CGPreflightPostEventAccess`,
    and `/health` reports it rather than failing silently.
  - **Linux (Hyprland/Wayland):** `wtype -- <text>`, passed as an exec argument and never through a
    shell. Paste runs `wl-copy --foreground --paste-once`, sends Ctrl+Shift+V, and waits for
    `wl-copy` to exit before restoring the old clipboard, instead of sleeping a fixed time. The
    helper never captures stdout or stderr from processes that fork, such as a background `wl-copy`.
- `pair`, `devices` and `revoke` subcommands, as above.

## Testing

- **Go:** unit tests with a fake typer cover auth, the rate limit, the 1-second lock and 409, the
  hardening limits, the tailnet filter, the Host allowlist, the type-versus-paste rules, newline
  handling, pairing, lockout, revoke and token reload. Linux backend tests inject the command
  runner and check exact argument lists.
- **Linux typing is checked by hand.** There's no headless Wayland test in CI.
- **iOS:** unit tests for the model and seeding, the Keychain store (run in a test host app), and
  the HTTP client against a stub, plus a UI test target for tap and double-tap confirm.
- **Manual checklist** on both machines, including Unicode ("naïve café ✓") and a multi-line
  phrase.

## Install (personal use only)

- **iOS:** internal TestFlight only, for Eric's own iPhone and iPad. There are no external testers
  and no App Store release. It needs an App Store Connect app record for
  `dev.ericdahl.phrasedeck`, which Eric creates, and then an archive and upload. The app uses no
  encryption beyond what the OS provides, so `ITSAppUsesNonExemptEncryption` is `NO`. During
  development the app still runs straight from Xcode on his devices.
- **macOS:** build locally with `make`, which signs `phrased` with a stable identity (Eric's Apple
  Development certificate), so the Accessibility grant survives rebuilds. It runs from
  `~/Library/LaunchAgents/dev.ericdahl.phrased.plist`. It doesn't need notarizing, but it does need
  Accessibility permission once.
- **omarchy:** `pacman -S wtype wl-clipboard`, install the binary, plus a systemd user unit tied to
  the graphical session. The unit doesn't hardcode `WAYLAND_DISPLAY`; it takes it from the
  uwsm/graphical-session environment, with a `ConditionEnvironment=WAYLAND_DISPLAY` guard.

## Repo layout

```
ios/       SwiftUI app
helper/    Go module for phrased
docs/      design and plans
```

## Out of scope for v1

Per-app pages, sequences with delays, icons, and choosing a session or window from the phone.
