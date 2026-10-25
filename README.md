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

### Install on the Mac (runs at login)

```sh
cd helper
make install-mac
```

This builds and signs `phrased`, installs it to `~/.local/bin/phrased`, and loads the LaunchAgent
`~/Library/LaunchAgents/dev.ericdahl.phrased.plist`, which starts it at login and restarts it if
it stops. The log is `~/Library/Logs/phrased.log`. Run it again after pulling changes: it swaps in
the new binary and reloads the agent.

- **The first install** puts `phrased` in a new place, so grant Accessibility to
  `~/.local/bin/phrased` once and run the firewall commands that `make install-mac` prints. The
  stable signature keeps both through later installs.
- It reads `~/.config/phrased`, the same as running `./phrased` by hand.
- **Stop it** with `launchctl bootout gui/$(id -u)/dev.ericdahl.phrased`; remove the plist too to
  keep it from starting at the next login.

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
  anything over 300 characters, and types the rest. `type` types newlines as Shift+Enter. On the
  Mac, pasting isn't supported yet (#4), so those requests get 501.
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
| 501 | paste not supported yet on this computer |
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
