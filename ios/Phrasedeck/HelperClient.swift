import Foundation
import Network
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
        /// A LAN IP or .local name on the plain-HTTP route: refused, nothing sent.
        case notTailnet
        /// The OS refused the request before sending it (ATS, a bad URL): nothing was typed.
        case notSent(Int)
    }

    static let timeout: TimeInterval = 3

    let helper: Helper
    let token: String?
    var route: Route = .tailnet
    var session: URLSession = .shared
    /// A host name's addresses, as strings. Tests stub it.
    typealias Resolve = @Sendable (String) async -> [String]
    var resolve: Resolve = HelperClient.systemResolve
    /// Sends the tailnet route's plain HTTP to the checked address. Network.framework in the
    /// app; through the injected session in tests.
    var plainSend: PlainSend = HelperClient.networkSend

    init(helper: Helper, token: String?, route: Route = .tailnet, session: URLSession? = nil,
         resolve: Resolve? = nil, plainSend: PlainSend? = nil) {
        self.helper = helper
        self.token = token
        self.route = route
        if let resolve { self.resolve = resolve }
        if let plainSend { self.plainSend = plainSend } else if let session { self.plainSend = Self.sessionSend(session) }
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

    /// The address plain HTTP may go to. With Tailscale off, a bare name can resolve through
    /// the router's DNS to a LAN machine, so a name's addresses must all be tailnet ones, and
    /// the request goes to the checked one (IPv4 first): there's no second lookup to swap it.
    private func checkedTailnetAddress(_ url: URL?) async throws -> String {
        guard let host = url.flatMap({ URLComponents(url: $0, resolvingAgainstBaseURL: false)?.host })?
                .trimmingCharacters(in: CharacterSet(charactersIn: "[]"))
        else { throw Failure.unreachable }
        // An IP literal was already checked by request().
        if IPv4Address(host) != nil && host.allSatisfy({ $0.isNumber || $0 == "." }) || IPv6Address(host) != nil {
            return host
        }
        let addresses = await resolve(host)
        guard !addresses.isEmpty else { throw Failure.unreachable }
        guard addresses.allSatisfy({ PairLink.tailnetHost($0) != nil }) else { throw Failure.notTailnet }
        return addresses.first { !$0.contains(":") } ?? addresses[0]
    }

    /// A PlainSend through a URLSession, for tests' stub sessions: the URL is rewritten to the
    /// address so the stub records where the request went.
    static func sessionSend(_ session: URLSession) -> PlainSend {
        { request, address, port in
            var request = request
            if let url = request.url, var components = URLComponents(url: url, resolvingAgainstBaseURL: false),
               let host = components.host {
                request.setValue(components.port.map { "\(host):\($0)" } ?? host, forHTTPHeaderField: "Host")
                components.host = address.contains(":") ? "[\(address)]" : address
                components.port = port
                request.url = components.url
            }
            let (data, response) = try await session.data(for: request)
            return ((response as? HTTPURLResponse)?.statusCode ?? 0, data)
        }
    }

    /// getaddrinfo, off the main thread. No answer is an empty list.
    static let systemResolve: Resolve = { name in
        await Task.detached {
            var hints = addrinfo(ai_flags: 0, ai_family: AF_UNSPEC, ai_socktype: SOCK_STREAM,
                                 ai_protocol: 0, ai_addrlen: 0, ai_canonname: nil, ai_addr: nil, ai_next: nil)
            var list: UnsafeMutablePointer<addrinfo>?
            guard getaddrinfo(name, nil, &hints, &list) == 0, let first = list else { return [] }
            defer { freeaddrinfo(first) }
            var out: [String] = []
            for info in sequence(first: first, next: { $0.pointee.ai_next }) {
                var buffer = [CChar](repeating: 0, count: Int(NI_MAXHOST))
                if getnameinfo(info.pointee.ai_addr, info.pointee.ai_addrlen, &buffer, socklen_t(buffer.count),
                               nil, 0, NI_NUMERICHOST) == 0 {
                    // Drop an IPv6 zone like %en0; it's never a tailnet address anyway.
                    let text = String(cString: buffer).split(separator: "%").first.map(String.init) ?? ""
                    if !text.isEmpty, !out.contains(text) { out.append(text) }
                }
            }
            return out
        }.value
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
        // The token never goes over plain HTTP to a LAN address or .local name.
        if route == .tailnet, !PairLink.isTailnetRouteHost(host) { throw Failure.notTailnet }
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
        let status: Int
        let pinning: PinningDelegate? = if case let .lan(_, _, pin) = route { PinningDelegate(pin: pin) } else { nil }
        do {
            if route == .tailnet {
                let address = try await checkedTailnetAddress(request.url)
                (status, data) = try await plainSend(request, address, request.url?.port ?? helper.port)
            } else {
                let response: URLResponse
                (data, response) = try await session.data(for: request, delegate: pinning)
                status = (response as? HTTPURLResponse)?.statusCode ?? 0
            }
        } catch let error as URLError {
            throw Self.failure(for: error.code, pinMismatched: pinning?.mismatched ?? false)
        }

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
        case .cannotConnectToHost, .cannotFindHost, .dnsLookupFailed, .notConnectedToInternet:
            return .unreachable
        case .appTransportSecurityRequiresSecureConnection, .badURL, .unsupportedURL:
            return .notSent(code.rawValue)
        default:
            // Timeouts, a dropped connection and anything unrecognized: the
            // request may have reached the helper, so don't claim it didn't.
            return .noReply
        }
    }
}
