import Foundation
import Synchronization

struct Health: Decodable, Equatable, Sendable {
    let hostname: String
    let os: String
    let accessibilityOK: Bool

    enum CodingKeys: String, CodingKey {
        case hostname, os
        case accessibilityOK = "accessibility_ok"
    }
}

/// What `/pair` returns: the token, plus the LAN certificate's fingerprint
/// and port when the helper serves the LAN (#11).
struct Pairing: Equatable, Sendable {
    let token: String
    var certSHA256: String?
    var lanPort: Int?
}

/// What `GET /info` returns; both nil when the helper has no LAN listener.
struct LANInfo: Equatable, Sendable {
    var certSHA256: String?
    var lanPort: Int?
}

/// The LAN fields as the helper sends them, checked: a fingerprint that
/// isn't 64 hex digits or a port out of range is dropped.
private struct LANFields: Decodable {
    let certSHA256: String?
    let lanPort: Int?

    var fingerprint: String? { certSHA256.flatMap(Fingerprint.normalize) }
    var port: Int? { lanPort.flatMap { (1...65535).contains($0) ? $0 : nil } }
}

/// Talks to one `phrased` helper: plain HTTP inside the tailnet, or HTTPS
/// to its LAN address with the certificate pinned (#11).
/// Every call makes exactly one request: nothing is ever retried, because
/// a retried `/type` could type the phrase twice.
struct HelperClient: Sendable {
    enum Route: Equatable, Sendable {
        case tailnet
        case lan(host: String, port: Int, pin: String)
    }

    enum Failure: Error, Equatable, Sendable {
        /// Timed out, lost mid-request, or 409: the phrase may have been typed.
        case noReply
        /// Refused, or the host name didn't resolve: nothing was typed.
        case unreachable
        case unauthorized
        /// `/pair` with no pairing window open: run `phrased pair` first.
        case noPairingWindow
        case notReady
        case rateLimited
        case server(Int)
        /// The LAN certificate wasn't the pinned one: nothing was sent.
        case pinMismatch
    }

    static let timeout: TimeInterval = 3

    let helper: Helper
    let token: String?
    var route: Route = .tailnet
    var session: URLSession = .shared

    init(helper: Helper, token: String?, route: Route = .tailnet, session: URLSession? = nil) {
        self.helper = helper
        self.token = token
        self.route = route
        self.session = session ?? {
            if case let .lan(_, _, pin) = route { return Self.lanSession(for: pin) }
            return .shared
        }()
    }

    /// One ephemeral session per pin. A session reuses its TLS connections
    /// without asking the delegate again, so a connection checked against one
    /// pin must never carry a request that expects another.
    private static func lanSession(for pin: String) -> URLSession {
        lanSessions.withLock { sessions in
            if let session = sessions[pin] { return session }
            let session = URLSession(configuration: .ephemeral)
            sessions[pin] = session
            return session
        }
    }

    /// Drops a pin's session and cancels its connections, when a computer's
    /// fingerprint changes or it's deleted.
    static func forgetLANSession(pin: String) {
        lanSessions.withLock { $0.removeValue(forKey: pin) }?.invalidateAndCancel()
    }

    // One entry per paired computer's certificate; forgetLANSession removes stale ones.
    private static let lanSessions = Mutex<[String: URLSession]>([:])

    func health() async throws -> Health {
        let data = try await send(request("GET", "/health"))
        return try JSONDecoder().decode(Health.self, from: data)
    }

    func pair(code: String, device: String) async throws -> Pairing {
        struct Reply: Decodable { let token: String }
        let data = try await send(request("POST", "/pair", body: ["code": code, "device": device]),
                                  notFound: .noPairingWindow)
        let token = try JSONDecoder().decode(Reply.self, from: data).token
        let lan = try? JSONDecoder().decode(LANFields.self, from: data)
        return Pairing(token: token, certSHA256: lan?.fingerprint, lanPort: lan?.port)
    }

    /// The LAN certificate fingerprint and port, over an authenticated
    /// request. A helper without `/info` (404) has nothing to tell.
    func info() async throws -> LANInfo {
        let data: Data
        do {
            data = try await send(request("GET", "/info", authorized: true))
        } catch Failure.server(404) {
            return LANInfo()
        }
        let lan = try JSONDecoder().decode(LANFields.self, from: data)
        return LANInfo(certSHA256: lan.fingerprint, lanPort: lan.port)
    }

    /// Always `mode: "auto"`: the helper decides between typing and pasting.
    func type(text: String, enter: Bool) async throws {
        struct Body: Encodable { let text: String; let enter: Bool; let mode = "auto" }
        _ = try await send(request("POST", "/type", body: Body(text: text, enter: enter), authorized: true))
    }

    private func request(_ method: String, _ path: String, body: (some Encodable)? = String?.none,
                         authorized: Bool = false) throws -> URLRequest {
        let (scheme, host, port) = switch route {
        case .tailnet: ("http", helper.host, helper.port)
        case let .lan(host, port, _): ("https", host, port)
        }
        var components = URLComponents()
        components.scheme = scheme
        // URLComponents wants IPv6 literals in brackets.
        components.host = host.contains(":") && !host.hasPrefix("[") ? "[\(host)]" : host
        components.port = port
        components.path = path
        guard let url = components.url else { throw Failure.unreachable }

        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: Self.timeout)
        request.httpMethod = method
        if let body {
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = try JSONEncoder().encode(body)
        }
        if authorized, let token {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        return request
    }

    private func send(_ request: URLRequest, notFound: Failure = .server(404)) async throws -> Data {
        let data: Data
        let response: URLResponse
        let pinning: PinningDelegate? = if case let .lan(_, _, pin) = route { PinningDelegate(pin: pin) } else { nil }
        do {
            (data, response) = try await session.data(for: request, delegate: pinning)
        } catch let error as URLError {
            throw Self.failure(for: error.code, pinMismatched: pinning?.mismatched ?? false)
        }

        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        switch status {
        case 200..<300: return data
        case 401: throw Failure.unauthorized
        case 404: throw notFound
        case 409: throw Failure.noReply
        case 429: throw Failure.rateLimited
        case 503: throw Failure.notReady
        default: throw Failure.server(status)
        }
    }

    static func failure(for code: URLError.Code, pinMismatched: Bool = false) -> Failure {
        // A refused pin surfaces as a cancel; it must read as a red error, never "may have typed".
        if pinMismatched { return .pinMismatch }
        switch code {
        case .cannotConnectToHost, .cannotFindHost, .dnsLookupFailed:
            return .unreachable
        default:
            // Timeouts, a dropped connection and anything unrecognized: the
            // request may have reached the helper, so don't claim it didn't.
            return .noReply
        }
    }
}
