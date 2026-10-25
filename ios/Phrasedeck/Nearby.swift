import Foundation

/// Where a helper answers on the home network, as Bonjour resolved it.
struct LANAddress: Equatable, Hashable, Sendable {
    let host: String
    let port: Int
}

/// A `_phrasedeck._tcp` service seen on the LAN (#11). TXT is
/// unauthenticated: its `ts` name and `fp` only decide which paired
/// computer is nearby. `fp` is never stored as a pin, and the address is
/// used only with a pin learned from /pair, the QR code or /info, so a
/// spoofed TXT just fails the handshake.
struct NearbyService: Equatable, Hashable, Sendable {
    let tailnetName: String?
    var address: LANAddress?
    /// A match key for "nearby", not trust: compared with stored pins only.
    var advertisedFingerprint: String?

    init(tailnetName: String?, address: LANAddress?, advertisedFingerprint: String? = nil) {
        self.tailnetName = tailnetName
        self.address = address
        self.advertisedFingerprint = advertisedFingerprint
    }

    /// From TXT `v=1` with `ts=<MagicDNS name>`, `fp=<fingerprint>` or both;
    /// nil when neither is valid.
    init?(txt: [String: String], address: LANAddress?) {
        guard txt["v"] == "1" else { return nil }
        let ts = txt["ts"].flatMap(PairLink.tailnetHost).flatMap { $0.hasSuffix(".ts.net") ? $0 : nil }
        let fp = txt["fp"].flatMap(Fingerprint.normalize)
        guard ts != nil || fp != nil else { return nil }
        self.init(tailnetName: ts, address: address, advertisedFingerprint: fp)
    }

    /// Whether this service is the paired computer: by tailnet name, or by
    /// the advertised fingerprint equaling its stored pin.
    func matches(_ helper: Helper) -> Bool {
        if helper.hasTailnetHost, let tailnetName, Helper.sameMachine(helper.host, tailnetName) { return true }
        if let pin = helper.certSHA256, let advertisedFingerprint, pin == advertisedFingerprint { return true }
        return false
    }
}
