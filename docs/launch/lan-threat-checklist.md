# LAN access (#11): threat checklist

Both #11 PRs are reviewed against this list. Each item is something to check in the code and,
where it can be, to try on a scratch config and port. The design is in the decision comment on #11.

**What's at stake:** a paired device's bearer token lets anyone type into the focused window on
Eric's computer. A prompt is often a command to a coding agent, so it amounts to code execution.
The tailnet route (47811, plain HTTP inside WireGuard) is unchanged. The new risk is the LAN
listener (47812) and the app's choice of route.

## Helper

**LAN attacker on the same Wi-Fi**
- [ ] 47812 serves TLS only (no plain-HTTP fallback on that port), with TLS 1.2 or newer.
- [ ] The source-IP filter accepts only `-lan` prefixes and tailnet addresses, and runs before the
      TLS handshake. Addresses are `Unmap()`ed first, so `::ffff:192.168.1.5` gets the same answer.
- [ ] `-lan` refuses an address that isn't a prefix (`192.168.1.5`) and rejects or loudly warns on
      prefixes that aren't private (outside 10/8, 172.16/12, 192.168/16, fc00::/7, fe80::/10), or
      broader than /16. `0.0.0.0/0` must never be accepted quietly.
- [ ] **One rate limiter and one lockout for both listeners.** `server.New` creates the limiters, so
      calling it once per listener doubles the budget. Share one handler, or pass the limiters in.
- [ ] The log for a dropped connection no longer says "not a tailnet address" on 47812.
- Try: connect to 47812 from an address outside `-lan` (closed with no TLS answer), send plain HTTP
  to 47812 (fails), and burst `/pair` across both ports (limit is shared).

**Host header and DNS rebinding**
- [ ] 47812 runs the same `hostGuard` as 47811. The allowlist adds only this machine's LAN IPs (in
      the `-lan` prefixes), `<hostname>.local` and `phrased-<hostname>.local`, never a wildcard.
- [ ] A rebinding page (`Host: attacker.example`) gets 403 before any handler runs. Browsers also
      refuse the self-signed cert, but the Host check must not depend on that.
- [ ] If DHCP changes the IP, the helper either refreshes the allowlist or says in its log why
      requests get 403.
- Try: `curl -k -H 'Host: evil.example' https://<lan-ip>:47812/health` returns 403.

**Certificate and key file**
- [ ] ECDSA P-256 key, written 0600 through a temp file and rename (like `auth.writePrivate`), in the
      0700 config dir. It's never logged and never returned by any endpoint.
- [ ] If the key is unreadable or corrupt, the helper stops with an error. It never quietly makes a
      new key, which would change the pin; regenerating takes an explicit step.
- [ ] The fingerprint is SHA-256 over the leaf certificate's DER bytes, as lowercase hex, computed
      the same way on the helper and in the app.
- Try: `stat -f %Lp` (macOS) or `stat -c %a` (Linux) on the key shows 600, and a second start
  reuses it (same fingerprint).

**Bonjour**
- [ ] It advertises only on the interfaces that match `-lan` (dnssd's `Config.Ifaces`), not on
      loopback, `utun*` or `tailscale0`.
- [ ] dnssd's `Host` is `phrased-<shorthost>`, not the system's `<host>.local`, so it never publishes
      a competing A/AAAA set that makes mDNSResponder or avahi rename the machine. If the name is
      taken, dnssd's probe renames it (`phrased-<host>-2`); the app uses the resolved IP, so nothing
      depends on the exact name.
- [ ] TXT holds only `v`, `port`, `fp` and (once Tailscale is up) `ts`, with no token or code.
      `fp` is the certificate fingerprint, which is public anyway since it's sent in every TLS
      handshake. It's there so the app can match a computer paired with Tailscale off. `ts` exposes
      the MagicDNS name to the LAN, which is acceptable but worth knowing.

**`/info` and `/pair`**
- [ ] `GET /info` needs a valid token and is served **only on the tailnet listener**. On 47812 a
      client could get the fingerprint over the connection the pin is meant to protect.
- [ ] `/pair` keeps the same code check, limits and lockout on both ports. It adds `lanPort` and
      `certSHA256` only when LAN is on.
- [ ] Known limitation: anyone on the LAN can use up the 10 wrong codes and lock pairing until
      `phrased pair` runs again. That's acceptable (it's not a bypass), but `phrased pair` should say so.

**With `-lan` unset**
- [ ] Nothing listens on 47812, there's no Bonjour, and no cert or key is generated. `/pair` and the
      QR are exactly as before, and `/info` says LAN is off. Existing behavior is unchanged.
- Try: start a scratch helper without `-lan`; `nc -z <ip> 47812` fails and no `cert.pem` appears.

## App

**Cert pin and downgrade**
- [ ] For a server-trust challenge on the LAN route, compare the leaf's SHA-256 with the stored
      fingerprint. A mismatch calls `.cancelAuthenticationChallenge`, **never**
      `.performDefaultHandling`, which would accept any cert that chains to a public CA.
- [ ] The token header is sent only on a session that has this pinning delegate.
- [ ] The app never sends `http://` to a LAN IP or a `.local` name. `NSAllowsLocalNetworking` is on
      for the tailnet route, so ATS won't stop it; the code has to.
- [ ] **Bare host names too.** With Tailscale off, a bare name like `omarchy-laptop` resolves
      through the router's DNS to a LAN address. Before any plain-HTTP request, every resolved
      address must be a tailnet address (100.64/10, fd7a:115c:a1e0::/48), or nothing is sent. The
      request (token included) goes out right after the TCP handshake, before the helper's filter
      can drop it, and `/info` over such a route could plant an attacker's pin.
- [ ] A pin mismatch is shown to the user and never retried over another LAN route. Falling back to
      the tailnet route after a LAN timeout or pin failure is fine (it's authenticated by WireGuard),
      but it must keep the tailnet host and never move the token onto plain HTTP.

**Spoofed Bonjour and `.local` names**
- [ ] Nothing from Bonjour is trusted. TXT `ts` and `fp` only decide "nearby" (`fp` is compared
      with stored pins, never stored), and the address it resolves to is pinned like any other.
- [ ] A spoofer claiming `ts=<Eric's Mac>` gets a TLS handshake that fails the pin, and no token is
      sent. The cost is a denial of service, so prefer falling back to the tailnet route with a
      visible note over failing every send.
- [ ] The app connects to the resolved IP, or to `<name>.local` with the pin. A `.local` name is
      spoofable, so the pin is what protects it, never the name.

**QR `lan` and `fp`**
- [ ] `fp` must be exactly 64 hex characters (normalized to lowercase), or the scan is rejected.
- [ ] `lan` must be an IP literal (IPv4, or IPv6 in brackets), private or link-local, with no
      hostname, path or port tricks. The QR has no LAN port: QR pairing uses 47812, so a
      non-default LAN port can't be paired by QR (documented). `port` must be in 1 to 65535.
- [ ] `v` stays 1. New fields are optional and unknown fields are ignored. A QR without `fp` never
      enables the LAN route.
- [ ] A malicious QR can pair the app with an attacker's helper, which would then receive every
      phrase. Add a computer shows the name and address before saving (as it does for `host` today).

**Pairing over LAN**
- [ ] With Tailscale off, pairing goes only to `https://<lan>:47812/pair`, pinned to the QR's
      `fp`. The fingerprint stored afterward must equal the QR's `fp`, or the pairing is rejected.
- [ ] Unpaired nearby computers from Bonjour can only prompt "Scan its QR code to pair". Never pair
      from a Bonjour result alone.
