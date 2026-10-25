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

### Grant Accessibility

macOS only lets `phrased` type after you allow it once:

1. Start it: `./phrased`. If it can't type yet, it logs `can't type yet: grant Accessibility`.
2. Open System Settings > Privacy & Security > Accessibility, click +, and add
   `helper/phrased` (or the terminal app you run it from, if macOS lists that instead).
3. `curl http://<tailnet-name>:47811/health` from a tailnet device shows `"accessibility_ok": true`.

### Preset a token

Pairing from the app comes later (#13). For now, make a token by hand and store only its hash:

```sh
mkdir -p ~/.config/phrased
TOKEN=$(openssl rand -hex 32)
HASH=$(printf %s "$TOKEN" | shasum -a 256 | cut -d' ' -f1)
printf '{"devices":[{"name":"curl","sha256":"%s","paired_at":"%s"}]}\n' \
  "$HASH" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > ~/.config/phrased/tokens.json
chmod 600 ~/.config/phrased/tokens.json
echo "$TOKEN"   # keep this; phrased only has the hash
```

`phrased` refuses a `tokens.json` that group or others can read. Restart it after editing the file.

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

Status codes: 400 bad JSON or mode, 401 bad token, 403 Host not allowed, 409 busy typing another
phrase for over a second, 413 body over 64 KB or text over 10,000 characters, 415 not
`application/json`, 429 over 10 requests a second, 501 paste not supported yet, 503 Accessibility
not granted.

### Flags

| Flag | Default | |
|---|---|---|
| `-port` | `47811` | port to listen on |
| `-config` | `~/.config/phrased` | directory holding `tokens.json` |
| `-tailscale` | `tailscale` on PATH, else the Mac app's CLI | used once at startup for this machine's names |
| `-allow-host` | none | another `Host` to accept; repeat for more |

If `tailscale status --json` fails, `phrased` exits: without the allowlist it would answer 403 to
everything.
