import Foundation

/// Where a helper answers on the home network, as Bonjour resolved it.
struct LANAddress: Equatable, Hashable, Sendable {
    let host: String
    let port: Int
}

/// A `_phrasedeck._tcp` service seen on the LAN (#11). Only the tailnet
/// name comes from TXT: TXT is unauthenticated, so it never supplies a
/// fingerprint, and the address is used only with a pin learned elsewhere.
struct NearbyService: Equatable, Hashable, Sendable {
    let tailnetName: String
    var address: LANAddress?

    init(tailnetName: String, address: LANAddress?) {
        self.tailnetName = tailnetName
        self.address = address
    }

    /// From TXT `v=1` and `ts=<MagicDNS name>`; nil for anything else.
    init?(txt: [String: String], address: LANAddress?) {
        guard txt["v"] == "1", let ts = txt["ts"].flatMap(PairLink.tailnetHost), ts.hasSuffix(".ts.net") else { return nil }
        self.init(tailnetName: ts, address: address)
    }
}
