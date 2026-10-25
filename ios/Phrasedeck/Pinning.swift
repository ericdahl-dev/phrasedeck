import CryptoKit
import Foundation
import Security

/// A helper's LAN certificate fingerprint: lowercase hex SHA-256 of the
/// leaf certificate's DER, 64 characters.
enum Fingerprint {
    /// Lowercases and drops colons; nil unless the result is 64 hex digits.
    static func normalize(_ raw: String) -> String? {
        let hex = raw.lowercased().filter { $0 != ":" }
        return hex.count == 64 && hex.allSatisfy(\.isHexDigit) && hex.allSatisfy(\.isASCII) ? hex : nil
    }

    static func of(_ der: Data) -> String {
        SHA256.hash(data: der).map { String(format: "%02x", $0) }.joined()
    }
}

/// Pins one request to the helper's self-signed LAN certificate: the
/// leaf's SHA-256 must equal the stored fingerprint, or the connection is
/// cancelled before anything (the token included) is sent. The pin, not the
/// host name or a CA, is what identifies the helper, so the address can
/// come from Bonjour.
final class PinningDelegate: NSObject, URLSessionTaskDelegate, @unchecked Sendable {
    let pin: String
    private let lock = NSLock()
    private var _mismatched = false

    /// Set when the server showed a certificate other than the pinned one.
    var mismatched: Bool { lock.withLock { _mismatched } }

    init(pin: String) { self.pin = pin }

    func urlSession(_ session: URLSession, task: URLSessionTask, didReceive challenge: URLAuthenticationChallenge)
        async -> (URLSession.AuthChallengeDisposition, URLCredential?) {
        guard challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust,
              let trust = challenge.protectionSpace.serverTrust
        else { return (.cancelAuthenticationChallenge, nil) }
        return decide(trust: trust)
    }

    func decide(trust: SecTrust) -> (URLSession.AuthChallengeDisposition, URLCredential?) {
        if Self.leaf(of: trust, matches: pin) { return (.useCredential, URLCredential(trust: trust)) }
        lock.withLock { _mismatched = true }
        return (.cancelAuthenticationChallenge, nil)
    }

    static func leaf(of trust: SecTrust, matches pin: String) -> Bool {
        guard let chain = SecTrustCopyCertificateChain(trust) as? [SecCertificate], let leaf = chain.first
        else { return false }
        return Fingerprint.of(SecCertificateCopyData(leaf) as Data) == pin
    }
}
