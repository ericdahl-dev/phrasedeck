import Foundation

struct Health: Decodable, Equatable, Sendable {
    let hostname: String
    let os: String
    let accessibilityOK: Bool

    enum CodingKeys: String, CodingKey {
        case hostname, os
        case accessibilityOK = "accessibility_ok"
    }
}

/// Talks to one `phrased` helper over plain HTTP inside the tailnet.
/// Every call makes exactly one request: nothing is ever retried, because
/// a retried `/type` could type the phrase twice.
struct HelperClient: Sendable {
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
    }

    static let timeout: TimeInterval = 3

    let helper: Helper
    let token: String?
    var session: URLSession = .shared

    func health() async throws -> Health {
        let data = try await send(request("GET", "/health"))
        return try JSONDecoder().decode(Health.self, from: data)
    }

    func pair(code: String, device: String) async throws -> String {
        struct Reply: Decodable { let token: String }
        let data = try await send(request("POST", "/pair", body: ["code": code, "device": device]),
                                  notFound: .noPairingWindow)
        return try JSONDecoder().decode(Reply.self, from: data).token
    }

    /// Always `mode: "auto"`: the helper decides between typing and pasting.
    func type(text: String, enter: Bool) async throws {
        struct Body: Encodable { let text: String; let enter: Bool; let mode = "auto" }
        _ = try await send(request("POST", "/type", body: Body(text: text, enter: enter), authorized: true))
    }

    private func request(_ method: String, _ path: String, body: (some Encodable)? = String?.none,
                         authorized: Bool = false) throws -> URLRequest {
        var components = URLComponents()
        components.scheme = "http"
        // URLComponents wants IPv6 literals in brackets.
        components.host = helper.host.contains(":") && !helper.host.hasPrefix("[") ? "[\(helper.host)]" : helper.host
        components.port = helper.port
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
        do {
            (data, response) = try await session.data(for: request)
        } catch let error as URLError {
            throw Self.failure(for: error.code)
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

    static func failure(for code: URLError.Code) -> Failure {
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
