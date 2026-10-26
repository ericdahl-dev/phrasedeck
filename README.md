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

**On a managed Mac** (a work Mac with an MDM profile), neither works: the + button is grayed out,
and `socketfilterfw` answers "cannot be modified from command line on managed Mac computers".
`phrased` then logs that the firewall is managed and lists any `phrased` path the firewall already
allows. Either:

- run `phrased` from that allowed path, for example by installing there:
  `make install-mac BIN_DIR=<the allowed folder>`; or
- ask IT to allow incoming connections for the path you want.

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
- `/health` reports `accessibility_ok: false` (the Helper isn't Ready) when a tool is missing or
  there's no display, and the error says which.
- Stop it with `systemctl --user disable --now phrased`.

### Install on Ubuntu, GNOME or KDE

At startup the Helper picks its keyboard tool from the session (`XDG_SESSION_TYPE`,
`XDG_CURRENT_DESKTOP`):

| Session | Types with | Clipboard |
|---|---|---|
| Hyprland, Sway and other wlroots compositors | `wtype` | `wl-clipboard` |
| GNOME or KDE on Wayland | `ydotool` | `wl-clipboard` |
| Any desktop on X11 | `xdotool` | `xclip` |

GNOME and KDE don't offer the virtual-keyboard protocol `wtype` needs, so there the Helper types
through `ydotool`, which goes through the kernel's `/dev/uinput`. It sends US keycodes, so a line
with anything beyond ASCII ("naïve café ✓") is pasted instead of typed. GNOME typing needs
Ubuntu 26.04 or later (ydotool 1.0.x); 24.04 and 25.10 ship ydotool 0.1.8, which doesn't work. On
Ubuntu with GNOME (Wayland):

```sh
sudo apt install ydotool wl-clipboard
sudo usermod -aG input "$USER"   # the package's udev rule gives the input group /dev/uinput
# log out and back in so the group applies, then:
systemctl --user enable --now ydotool   # ydotoold, the daemon ydotool talks to
cd helper
make install-linux
```

- The Helper isn't Ready until `ydotoold` is running: it checks for its socket at
  `$XDG_RUNTIME_DIR/.ydotool_socket` (or `$YDOTOOL_SOCKET`).
- On an X11 session, install `xdotool xclip` instead; no daemon or group is needed.
- Pasting works as on omarchy: it serves the phrase once, sends Ctrl+Shift+V (the terminal
  paste key), and puts the old clipboard text back.

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
- `phrase` and `edit` (sent by the ESP32 panel): the Phrase's id and its edit counter. The first
  time the Helper types a Phrase at a new counter, it shows a notification (osascript on macOS,
  `notify-send` on Linux), since an edit on the panel's web page could have changed what it types.
  The Helper remembers counters until it restarts.
- On the Mac, a paste puts the phrase on the clipboard, sends Cmd+V, and about half a second
  later puts back everything that was on the clipboard before (images and rich text too). If you
  copy something in that half second, your new copy is kept instead.
- `GET /health` needs no token and returns `hostname`, `os` and `accessibility_ok`.
- `GET /info` needs the token and returns `hostname`, plus `certSHA256` and `lanPort` when the
  LAN route is on. It's served over the tailnet only.

Status codes:

| Code | Meaning |
|---|---|
| 400 | bad JSON, mode or device name |
| 401 | bad token, or a wrong pairing code |
| 403 | `Host` not allowed |
| 404 | no pairing window open, or pairing locked |
| 409 | busy typing another phrase for over a second; nothing was typed |
| 413 | body over 64 KB, or text over 10,000 characters |
| 415 | not `application/json` |
| 429 | over 10 requests a second on `/type`, or about 1 a second on `/pair` |
| 500 | typing failed partway; some keys may have been typed |
| 503 | can't type yet: Accessibility not granted (macOS), or a typing tool missing, no display, or `ydotoold` not running (Linux); the error says which |

### Flags

| Flag | Default | |
|---|---|---|
| `-port` | `47811` | port to listen on |
| `-config` | `~/.config/phrased` | directory holding `tokens.json` and `pairing.json` |
| `-tailscale` | `tailscale` on PATH, else the Mac app's CLI | used once at startup for this machine's names |
| `-allow-host` | none | another `Host` to accept; repeat for more |
| `-lan` | none | a home subnet to also serve over HTTPS, like `192.168.1.0/24`; repeat for more |
| `-lan-port` | `47812` | HTTPS port for the `-lan` subnets |

If `tailscale status --json` fails, `phrased` exits: without the allowlist it would answer 403 to
everything. With `-lan` set it keeps running, serves only the LAN route, and checks again every 30 seconds;
once Tailscale is up the tailnet route works too, without a restart.

### Home network without Tailscale (optional)

`-lan 192.168.1.0/24` (or one subnet per line in `~/.config/phrased/lan-subnets`; `#` starts a
comment) adds a second listener:

- **HTTPS on port 47812,** open only to those subnets and to tailnet peers. Each subnet must be
  private (inside `10/8`, `172.16/12`, `192.168/16` or `fc00::/7`), so a typo can't expose the
  helper to the internet.
- **Its certificate** is self-signed ECDSA, made on first run as `lan-cert.pem` and `lan-key.pem`
  (both mode 0600) in the config directory and reused after that. The app pins its SHA-256, which
  `phrased` logs at startup, and gets it only over a trusted channel: `/pair` over the tailnet,
  `GET /info` over the tailnet, or the QR code. If either file is unreadable or corrupt, `phrased`
  stops with an error naming it rather than making a new one. Delete both files to make a new
  one, and then re-pair every device.
- **Bonjour** advertises `_phrasedeck._tcp` on the interfaces in those subnets, as
  `phrased-<host>.local`, with TXT `v=1`, `ts=<tailnet name>`, `fp` (the certificate
  fingerprint, for matching a paired computer) and `port`. TXT carries no token. If Tailscale
  comes up after `phrased` starts, `ts` is added without a restart.
- **`/pair`** adds `certSHA256` and `lanPort` to its reply, and the QR code from `phrased pair`
  adds `fp`, `lan` (this machine's LAN address) and `lanPort`. `phrased pair` also prints `Pin for a panel:`
  the first 16 hex of the fingerprint, which a panel without a camera takes with the code (#68). With Tailscale off, the QR code
  leaves out `host` and `port` and carries only the LAN route.
- `/pair`, `/type`, tokens, limits and lockout are shared by both listeners. Each accepts only
  its own `Host` names: tailnet names on 47811, LAN IPs and `.local` names on 47812.

Limits:
- The LAN addresses and interfaces are read at startup; restart `phrased` after the network
  changes.
- On the Mac, the firewall must allow the binary (see above); on a managed Mac that means the
  already-allowed path.
- On Linux, a firewall that blocks incoming connections (Omarchy turns `ufw` on) must allow the LAN
  port from each `-lan` subnet (use yours, not the example's). Bonjour still resolves, but
  connections time out. The firewalld rule is IPv4 only; for an `fc00::/7` subnet use
  `family="ipv6"`.

  ```sh
  sudo ufw allow from 192.168.1.0/24 to any port 47812 proto tcp
  # firewalld:
  sudo firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="192.168.1.0/24" port port="47812" protocol="tcp" accept' && sudo firewall-cmd --reload
  ```

```sh
curl -sk https://<lan-ip>:47812/health   # -k: the app pins the certificate instead
```

## The iOS app on TestFlight

Installs on Eric's iPhone and iPad go through internal TestFlight only: no external testers, no
Beta App Review and no App Store release. Day-to-day development still runs straight from Xcode.

### One-time setup (Eric, in App Store Connect)

1. **Bundle ID** (done): the explicit App ID `dev.ericdahl.phrasedeck` is registered, with iCloud
   on for the sync slice (#10).
2. **App record** (done): "phrasedeck by Eric Dahl", Apple ID 6826742858, SKU `phrasedeck`,
   primary language English (U.S.). The plain name was taken on the App Store; the home screen
   still shows `phrasedeck` from `CFBundleDisplayName`.
3. **Internal group.** In the new app, TestFlight > Internal Testing > + : name it `Eric`, leave
   "Enable automatic distribution" on, then add yourself as the only tester.
4. **API key.** Users and Access > Integrations > App Store Connect API > Team Keys > +: name
   `phrasedeck upload`, access Admin (automatic signing creates the App Store profile, which an App
   Manager key can't do). Download the `.p8` (Apple only offers it once) and note the Key ID and the
   Issuer ID shown above the list. Keep the `.p8` outside the repo, for example in
   `~/.appstoreconnect/private_keys/`.
5. **TestFlight app.** Install TestFlight from the App Store on the iPhone and the iPad, signed in
   with the same Apple ID.

The Apple Distribution certificate for team `5HR8E5CWR7` must be in the login keychain (check with
`security find-identity -v -p codesigning`).

### Release by tag (the usual way)

Tag a version on `main` and push the tag; `.github/workflows/release.yml` builds it and uploads it
to TestFlight:

```sh
git tag v0.2.0 && git push origin v0.2.0
```

- **Version and build:** the tag sets the version (`v0.2.0` becomes `0.2.0`), and the build number
  is the run number plus the attempt (for example `12.1`), so re-running a tag uploads a new, higher
  build. Both go on the `xcodebuild` command line; no file changes.
- **Only from `main`** or a `release/` branch: a tag on any other commit fails at "Check the tag".
- **Signing** uses one fixed Apple Distribution certificate, and `ios/scripts/ci_profile.py` gets
  (or makes) the App Store profile `Phrasedeck CI App Store` through the App Store Connect API. The
  workflow checks that the profile has CloudKit and production push before it archives. Nothing
  makes a new certificate, so Apple's certificate limit is never reached.
- **Secrets** (repository secrets): `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_P8` (the `.p8`
  contents), `DEVELOPMENT_TEAM`, `DIST_CERT_P12_BASE64` (the certificate and its key as a `.p12`,
  run through `base64`) and `DIST_CERT_PASSWORD`. The repository is public, but only a tag pushed
  here runs the workflow, never a pull request or a fork, so the secrets never reach outside code.
- **If it fails**, open the run's Archive or Upload step; the logs are also attached to the run as
  `release-logs`.

**Don't mix the two upload paths within one version.** A local upload's build number (for example
`202610251430`) is higher than any CI build number, so after one local upload at `0.2.0`, every
tagged `v0.2.0` run fails with `bundle version must be higher`. Use the local script for a version
no tag will ship, or set `BUILD_NUMBER` to match.

### Upload a build from this Mac

```sh
ASC_KEY_ID=… ASC_ISSUER_ID=… ASC_KEY_PATH=~/.appstoreconnect/private_keys/AuthKey_….p8 \
  ios/scripts/testflight.sh
```

`ios/scripts/testflight.sh` runs `xcodegen`, archives a Release build and uploads it with
`ios/ExportOptions.plist` (App Store Connect, automatic signing). The key is read from the
environment each run and is never committed.

- **Build number:** the UTC date and time to the minute (for example `202610251430`), so it always
  grows. Set `BUILD_NUMBER` to override it. The version is `MARKETING_VERSION` in `ios/project.yml`.
- **Export compliance** is answered in the app (`ITSAppUsesNonExemptEncryption` is `NO`), so the
  build doesn't stop to ask.
- `--no-upload` signs and writes `ios/build/export/Phrasedeck.ipa` without uploading.
- The build shows in TestFlight after Apple processes it, usually 5 to 15 minutes. Install it from
  the TestFlight app on each device.
- `bundle version must be higher` means a build with that number exists; wait a minute and run it
  again.

The app icon's source is `design/icon.svg`; the asset catalog holds the 1024 x 1024 PNG made from it.
