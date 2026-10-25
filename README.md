# phrasedeck

A Stream Deck-style iOS app for Eric's own use. Each button types an often-used phrase into the
focused window on his Mac or on omarchy-laptop, usually a Claude Code terminal.

- `helper/`: `phrased`, the Go helper that receives a phrase over the tailnet and types it.
- `ios/`: the SwiftUI app (in progress).
- `docs/plans/`: the design and the implementation plan.

## The helper (`phrased`)

`phrased` listens on port 47811 but only answers Tailscale peers (`100.64.0.0/10` and
`fd7a:115c:a1e0::/48`), and only for a `Host` that is this machine's tailnet name, MagicDNS name or
tailnet IP. There is no LAN access.

### Build (Mac)

```sh
cd helper
make build      # go build, then codesign with "Apple Development: Eric Dahl (D6LCAABBEQ)"
make test
```

The stable signature keeps the Accessibility grant when you rebuild. Use another identity with
`make build SIGN_IDENTITY="..."` (list yours with `security find-identity -v -p codesigning`).

### Let it through the macOS firewall

If the macOS firewall is on, it may drop incoming connections to `phrased` without asking,
especially in stealth mode. `phrased` logs a warning at startup when the firewall doesn't allow it.
`make firewall` prints the two commands to fix that; run them yourself:

```sh
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add "$PWD/phrased"
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp "$PWD/phrased"
```

Or add `helper/phrased` in System Settings > Network > Firewall > Options and set it to
"Allow incoming connections".

### Grant Accessibility

macOS only lets `phrased` type after you allow it once:

1. Start it: `./phrased`. If it can't type yet, it logs `can't type yet: grant Accessibility`.
2. Open System Settings > Privacy & Security > Accessibility, click +, and add
   `helper/phrased` (or the terminal app you run it from, if macOS lists that instead).
3. `curl http://<tailnet-name>:47811/health` from a tailnet device shows `"accessibility_ok": true`.

### Install on omarchy (Hyprland)

```sh
sudo pacman -S wtype wl-clipboard
cd helper
make install-linux
```

This builds `phrased`, installs it to `~/.local/bin/phrased`, and runs it as the systemd user
service `phrased`, started with the graphical session and restarted if it fails. Run it again to
update. The unit (`helper/install/phrased.service`) doesn't hardcode `WAYLAND_DISPLAY`: it takes it
from the session environment uwsm imports at login, and skips starting without one.

- Status and log: `systemctl --user status phrased` and `journalctl --user -u phrased`.
- Typing uses `wtype`. Pasting serves the phrase once with `wl-copy --foreground --paste-once`,
  sends Ctrl+Shift+V (the terminal paste key), waits for `wl-copy` to exit (at most 2 seconds),
  and puts the old clipboard text back. Only text is restored: a copied image is lost.
- `/health` reports `accessibility_ok: false` when `wtype` is missing or there's no Wayland
  display.
- Stop it with `systemctl --user disable --now phrased`.

### Pair a device

Each phone or tablet gets its own token. On the computer running `phrased`:

```sh
./phrased pair
```

It prints a 6-digit code, valid for 2 minutes, and on the Mac also shows it as a notification.
Enter the code in the app, or pair from curl:

```sh
curl -s http://<tailnet-name>:47811/pair -H 'Content-Type: application/json' \
  -d '{"code":"123456","device":"curl"}'
# {"token":"..."}
```

- The code works once. A wrong code gets 401; with no open window (never opened, expired or
  already used) `/pair` answers 404.
- After 10 wrong codes, pairing stays locked (404) until you run `phrased pair` again.
- `/pair` allows about one attempt a second, separately from `/type`.
- Only hashes are saved: the token's in `~/.config/phrased/tokens.json` and the code's in
  `pairing.json`, both mode 0600.

### Devices and revoke

```sh
./phrased devices          # name and pairing date of each device
./phrased revoke "curl"    # that device's token gets 401 from its next request
```

The running helper rereads `tokens.json` when it changes, so neither needs a restart. `pair`,
`devices` and `revoke` take the same `-config` directory as the helper.

### Type with curl

```sh
curl -s http://<tailnet-name>:47811/type \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"text":"what is the status of the agents? ask them if needed","enter":true}'
```

- `enter: true` presses Enter after the text; leave it out to type without sending.
- `mode` is `auto` (the default), `type` or `paste`. `auto` pastes any text with a newline and
  anything over 300 characters, and types the rest. `type` types newlines as Shift+Enter.
- On the Mac, a paste puts the phrase on the clipboard, sends Cmd+V, and about half a second
  later puts back everything that was on the clipboard before (images and rich text too). If you
  copy something in that half second, your new copy is kept instead.
- `GET /health` needs no token and returns `hostname`, `os` and `accessibility_ok`.

Status codes:

| Code | Meaning |
|---|---|
| 400 | bad JSON, mode or device name |
| 401 | bad token, or a wrong pairing code |
| 403 | `Host` not allowed |
| 404 | no pairing window open, or pairing locked |
| 409 | busy typing another phrase for over a second |
| 413 | body over 64 KB, or text over 10,000 characters |
| 415 | not `application/json` |
| 429 | over 10 requests a second on `/type`, or about 1 a second on `/pair` |
| 501 | this computer's backend can't paste |
| 503 | Accessibility not granted |

### Flags

| Flag | Default | |
|---|---|---|
| `-port` | `47811` | port to listen on |
| `-config` | `~/.config/phrased` | directory holding `tokens.json` and `pairing.json` |
| `-tailscale` | `tailscale` on PATH, else the Mac app's CLI | used once at startup for this machine's names |
| `-allow-host` | none | another `Host` to accept; repeat for more |

If `tailscale status --json` fails, `phrased` exits: without the allowlist it would answer 403 to
everything.
