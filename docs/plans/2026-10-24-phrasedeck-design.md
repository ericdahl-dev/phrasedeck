# phrasedeck design

Date: 2026-10-24
Status: approved design, not yet built

## Goal

A Stream Deck-style iOS app for Eric's own use. Each button types an often-used phrase into the
focused window on his Mac or on omarchy-laptop. The main target is a Claude Code terminal, for
prompts like "what is the status of the agents? ask them if needed".

Not a product: there's no App Store, no TestFlight and no other users.

## Pieces

1. **iOS app** (SwiftUI, iPhone and iPad): a grid of phrase buttons, in pages.
2. **`phrased`** (Go): one helper binary for macOS and Linux. It receives a phrase over the network
   and types it into the focused window.

The text always goes to whatever window has focus on the chosen computer. The phone only picks
the computer.

## Discovery, pairing and protocol

- The helper advertises `_phrased._tcp` with Bonjour on the LAN. Over Tailscale, you add the helper
  once by its tailnet name, for example `omarchy-laptop`.
- **Pairing:** the first connection makes the helper show a 6-digit code. On the Mac it's a
  notification; on Linux it's in the terminal or the journal. You enter the code on the phone, which
  gets a long-lived token and stores it in the Keychain. The helper keeps only token hashes, in
  `~/.config/phrased/tokens.json` (mode 0600).
- **Transport:** plain HTTP with a bearer token. It binds only to the tailnet IP (100.x) and the LAN
  IP, never to 0.0.0.0.
  - `POST /type` with `{text, enter: bool, mode: "auto"|"type"|"paste"}` returns 200 after the
    text has been typed.
  - `GET /health` returns `{hostname, os, accessibility_ok}`.
- Rate limit: 10 requests per second, so a stuck button can't flood the keyboard.

## iOS app

### Data

```
Phrase { id, label, text, pressEnter: Bool, confirm: Bool, color, page }
Page   { id, name, order }
Helper { id, name, host, port }      // token lives in the Keychain, keyed by Helper.id
```

Phrases and pages are stored in SwiftData and synced through iCloud (CloudKit) between the iPhone
and iPad. Tokens never sync, so each device pairs on its own.

### Screens

- **Main screen:** a computer picker at the top with a status dot. Green means reachable, red means
  unreachable, and amber means the Mac lacks Accessibility permission. Below it is a grid of
  buttons, one page per swipe: 3×4 on an iPhone, more on an iPad. Each button shows its label in
  large type, with a ⏎ badge if it presses Enter.
- **Tap** sends the phrase. Phrases marked `confirm` need a double-tap.
- **Long-press** edits a button: label, text, Enter, confirm, color and page, or delete it.
  A "+" tile adds a phrase.
- **Feedback:** a haptic tick on a tap, a green flash when the helper returns 200, and a red flash
  plus a banner on failure ("omarchy-laptop unreachable", "Accessibility off on Mac").
- **No automatic retry.** A retry could type the phrase twice.
- A send times out after 3 seconds.

### Starter phrases

| Label | Text | Enter | Confirm |
|---|---|---|---|
| Agent status | what is the status of the agents? ask them if needed | yes | no |
| Status table | what is the status of the agents? ask them if needed.  show me in a table | yes | no |
| Merge… | `merge ` | no | no |
| Yes | yes | yes | yes |
| Merge it | Merge it | yes | yes |

## `phrased` helper (Go)

- `server`: `net/http`, the interface binding above, a constant-time token check and the rate limit.
- `typer` interface: `Type(text)`, `Paste(text)`, `Enter()`.
  - **macOS:** CGEvent with `CGEventKeyboardSetUnicodeString` in 20-character chunks, through cgo.
    Paste saves the clipboard, sets it, sends Cmd+V, then restores the clipboard. It needs
    Accessibility permission, and reports that in `/health` rather than failing silently.
  - **Linux (Hyprland/Wayland):** `wtype -- <text>`, passed as an exec argument and never through a
    shell. Paste uses `wl-copy` plus Ctrl+Shift+V.
  - **`auto` mode:** type up to about 300 characters and paste anything longer. Claude Code collapses
    long pastes into "[Pasted text]", so short phrases are typed.
- **Newlines** inside a phrase are typed as Shift+Enter, so a multi-line phrase doesn't send early.
  The final Enter comes only from the `enter` flag.
- `pair`: generates the code and stores the hashes.
- `mdns`: Bonjour advertising.

## Testing

- **Go:** unit tests with a fake typer cover auth, the rate limit, the type-versus-paste rule,
  newline handling and pairing. The Linux integration test runs `wtype` in a headless Wayland
  session (`sway`) in CI.
- **iOS:** unit tests for the model and for the HTTP client against a fake server, plus a UI test
  for tap and double-tap confirm.
- **Manual checklist** on both machines, including Unicode ("naïve café ✓") and a multi-line
  phrase.

## Install (personal use only)

- **iOS:** build and run from Xcode straight onto Eric's iPhone and iPad with his developer account.
  There's no App Store Connect record and no TestFlight. Reinstall when the provisioning profile
  expires after a year.
- **macOS:** build locally and run from `~/Library/LaunchAgents/dev.ericdahl.phrased.plist`.
  It doesn't need notarizing, but it does need Accessibility permission once.
- **omarchy:** `go install`, `pacman -S wtype wl-clipboard`, plus a systemd user unit.

## Repo layout

```
ios/       SwiftUI app
helper/    Go module for phrased
docs/      design and plans
```

## Out of scope for v1

Per-app pages, sequences with delays, icons, and choosing a session or window from the phone.
