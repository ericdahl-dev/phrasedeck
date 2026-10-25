import Foundation
import Network

/// The payload of the QR code `phrased pair` prints:
/// phrasedeck://pair?host=<MagicDNS name or tailnet IP>&port=<port>&code=<6 digits>&v=1.
/// It comes from a camera, so it's checked before the app contacts anything:
/// only tailnet hosts, a 6-digit code and version 1.
/// With LAN on (#11) it also carries `fp` (the helper's certificate
/// fingerprint), `lan` (its private LAN address) and `lanPort`, so pairing
/// can run over pinned TLS. With Tailscale off there is no `host` at all,
/// and then `lan` and `fp` are required.
struct PairLink: Equatable, Sendable {
    static let defaultLANPort = 47812

    let host: String?
    let port: Int
    let code: String
    var lan: String?
    var fingerprint: String?
    var lanPort: Int = PairLink.defaultLANPort

    enum Invalid: Error, CaseIterable {
        case notAPairingLink, unsupportedVersion, notATailnetHost, badCode, badPort
        case badFingerprint, badLANAddress, lanWithoutFingerprint

        var message: String {
            switch self {
            case .notAPairingLink: "That isn't a phrasedeck pairing code."
            case .unsupportedVersion: "This pairing code is from a newer phrased. Update phrasedeck."
            case .notATailnetHost: "That code points outside your tailnet, so phrasedeck won't contact it."
            case .badCode: "The pairing code in that link isn't 6 digits."
            case .badPort: "The port in that link isn't valid."
            case .badFingerprint: "The certificate fingerprint in that link isn't valid."
            case .badLANAddress: "The home network address in that link isn't a private address, so phrasedeck won't use it."
            case .lanWithoutFingerprint: "That link has a home network address but no certificate fingerprint to check it with."
            }
        }
    }

    init(host: String?, port: Int, code: String, lan: String? = nil, fingerprint: String? = nil,
         lanPort: Int = PairLink.defaultLANPort) {
        self.host = host
        self.port = port
        self.code = code
        self.lan = lan
        self.fingerprint = fingerprint
        self.lanPort = lanPort
    }

    /// A scanned QR code or an opened link, as the pairing sheet wants it.
    static func parse(_ text: String) -> Result<PairLink, Invalid> {
        guard let url = URL(string: text) else { return .failure(.notAPairingLink) }
        return Result { () throws(Invalid) in try PairLink(url: url) }
    }

    init(url: URL) throws(Invalid) {
        guard url.scheme?.lowercased() == "phrasedeck", url.host()?.lowercased() == "pair",
              let items = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems
        else { throw .notAPairingLink }
        func value(_ name: String) -> String? { items.first { $0.name == name }?.value }

        guard value("v") == "1" else { throw .unsupportedVersion }
        // A host, when present, must be a tailnet one; without it, lan and fp are checked below.
        var host: String?
        if let raw = value("host") {
            guard let h = Self.tailnetHost(raw) else { throw .notATailnetHost }
            host = h
        }
        guard let code = value("code"), code.count == 6, code.allSatisfy({ ("0"..."9").contains($0) })
        else { throw .badCode }
        var port = Helper.defaultPort
        if let text = value("port") {
            guard let p = Int(text), (1...65535).contains(p) else { throw .badPort }
            port = p
        }
        var fingerprint: String?
        if let raw = value("fp") {
            guard let fp = Fingerprint.normalize(raw) else { throw .badFingerprint }
            fingerprint = fp
        }
        var lan: String?
        if let raw = value("lan") {
            guard let address = Self.privateLANAddress(raw) else { throw .badLANAddress }
            guard fingerprint != nil else { throw .lanWithoutFingerprint }
            lan = address
        }
        var lanPort = Self.defaultLANPort
        if let text = value("lanPort") {
            guard let p = Int(text), (1...65535).contains(p) else { throw .badPort }
            lanPort = p
        }
        guard host != nil || (lan != nil && fingerprint != nil) else { throw .notATailnetHost }
        self.init(host: host, port: port, code: code, lan: lan, fingerprint: fingerprint, lanPort: lanPort)
    }

    /// The address, lowercased and unbracketed, if it's a private IPv4
    /// (RFC 1918) or IPv6 (fc00::/7) literal; otherwise nil. Link-local is
    /// left out, as on the helper.
    static func privateLANAddress(_ raw: String) -> String? {
        var address = raw.lowercased()
        if address.hasPrefix("["), address.hasSuffix("]") { address = String(address.dropFirst().dropLast()) }
        if let v4 = IPv4Address(address), address.allSatisfy({ $0.isNumber || $0 == "." }) {
            let b = [UInt8](v4.rawValue)
            let isPrivate = b[0] == 10 || (b[0] == 172 && (16...31).contains(b[1])) || (b[0] == 192 && b[1] == 168)
            return isPrivate ? address : nil
        }
        guard address.contains(":"), !address.contains("%"), let v6 = IPv6Address(address) else { return nil }
        let b = [UInt8](v6.rawValue)
        return b[0] & 0xfe == 0xfc ? address : nil
    }

    /// The name shown in the app: the machine name from a MagicDNS name, or the IP.
    var name: String { host.map(Helper.displayName) ?? lan ?? "" }

    /// Whether plain HTTP with the token may go to this host: a tailnet name
    /// (or a bare short name, which MagicDNS resolves) or a tailnet IP; never
    /// a LAN IP or a spoofable .local name.
    static func isTailnetRouteHost(_ raw: String) -> Bool {
        var host = raw.lowercased().trimmingCharacters(in: CharacterSet(charactersIn: "[]"))
        if host.hasSuffix(".") { host.removeLast() }
        guard !host.isEmpty, !host.hasSuffix(".local"), host != "local" else { return false }
        let isIP = IPv6Address(host) != nil || (IPv4Address(host) != nil && host.allSatisfy { $0.isNumber || $0 == "." })
        return isIP ? tailnetHost(host) != nil : true
    }

    /// The host, lowercased, if it's a *.ts.net name or a Tailscale address
    /// (100.64.0.0/10 or fd7a:115c:a1e0::/48); otherwise nil.
    static func tailnetHost(_ raw: String) -> String? {
        var host = raw.lowercased().trimmingCharacters(in: CharacterSet(charactersIn: "[]"))
        if host.hasSuffix(".") { host.removeLast() }
        if let v4 = IPv4Address(host), host.allSatisfy({ $0.isNumber || $0 == "." }) {
            let b = [UInt8](v4.rawValue)
            return b[0] == 100 && (64...127).contains(b[1]) ? host : nil
        }
        if host.contains(":") {
            guard let v6 = IPv6Address(host) else { return nil }
            return [UInt8](v6.rawValue).starts(with: [0xfd, 0x7a, 0x11, 0x5c, 0xa1, 0xe0]) ? host : nil
        }
        let labels = host.split(separator: ".", omittingEmptySubsequences: false)
        let validLabel: (Substring) -> Bool = { label in
            !label.isEmpty && label.count <= 63 && label.first != "-" && label.last != "-"
                && label.allSatisfy { ("a"..."z").contains($0) || ("0"..."9").contains($0) || $0 == "-" }
        }
        guard labels.count >= 3, labels.suffix(2) == ["ts", "net"], labels.allSatisfy(validLabel) else { return nil }
        return host
    }
}
