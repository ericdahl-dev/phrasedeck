import Foundation
import Network

/// The payload of the QR code `phrased pair` prints:
/// phrasedeck://pair?host=<MagicDNS name or tailnet IP>&port=<port>&code=<6 digits>&v=1.
/// It comes from a camera, so it's checked before the app contacts anything:
/// only tailnet hosts, a 6-digit code and version 1.
struct PairLink: Equatable, Sendable {
    let host: String
    let port: Int
    let code: String

    enum Invalid: Error, CaseIterable {
        case notAPairingLink, unsupportedVersion, notATailnetHost, badCode, badPort

        var message: String {
            switch self {
            case .notAPairingLink: "That isn't a phrasedeck pairing code."
            case .unsupportedVersion: "This pairing code is from a newer phrased. Update phrasedeck."
            case .notATailnetHost: "That code points outside your tailnet, so phrasedeck won't contact it."
            case .badCode: "The pairing code in that link isn't 6 digits."
            case .badPort: "The port in that link isn't valid."
            }
        }
    }

    init(host: String, port: Int, code: String) {
        self.host = host
        self.port = port
        self.code = code
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
        guard let host = value("host").flatMap(Self.tailnetHost) else { throw .notATailnetHost }
        guard let code = value("code"), code.count == 6, code.allSatisfy({ ("0"..."9").contains($0) })
        else { throw .badCode }
        var port = Helper.defaultPort
        if let text = value("port") {
            guard let p = Int(text), (1...65535).contains(p) else { throw .badPort }
            port = p
        }
        self.init(host: host, port: port, code: code)
    }

    /// The name shown in the app: the machine name from a MagicDNS name, or the IP.
    var name: String { Helper.displayName(for: host) }

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
