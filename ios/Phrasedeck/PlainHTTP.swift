import Foundation
import Network

extension HelperClient {
    /// The most a reply may be: the helper's are a few hundred bytes.
    static let maxReply = 1 << 20

    /// Sends a plain-HTTP request to `address:port` and returns the status and body.
    typealias PlainSend = @Sendable (URLRequest, _ address: String, _ port: Int) async throws -> (Int, Data)

    /// The tailnet route's sender (#71): Network.framework to the address that passed the
    /// tailnet check, so there's no second lookup that could swap it, and no ATS, which
    /// refuses plain HTTP to a bare 100.x address. The URL's host goes in the Host header.
    /// HTTP/1.0 with Connection: close, so the helper answers without chunking and hangs up.
    static let networkSend: PlainSend = { request, address, port in
        guard let payload = serialize(request), (1...65535).contains(port),
              let nwPort = NWEndpoint.Port(rawValue: UInt16(port))
        else { throw Failure.notSent(URLError.badURL.rawValue) }
        let exchange = PlainExchange(NWConnection(host: NWEndpoint.Host(address), port: nwPort, using: .tcp))
        return try await exchange.run(payload, timeout: timeout)
    }

    /// Whether a connection waiting with this error should give up now. Only a refused port
    /// (nothing listening) does; no route yet or a tunnel still waking after sleep gets the
    /// whole timeout to come up.
    static func stopsWaiting(_ error: NWError) -> Bool {
        if case .posix(.ECONNREFUSED) = error { return true }
        return false
    }

    /// The request as HTTP/1.0 bytes, or nil if it has no URL or a header would break a line.
    static func serialize(_ request: URLRequest) -> Data? {
        guard let url = request.url, let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
              var host = components.host else { return nil }
        if host.contains(":") { host = "[\(host)]" }
        if let port = components.port { host += ":\(port)" }
        let path = (components.percentEncodedPath.isEmpty ? "/" : components.percentEncodedPath)
            + (components.percentEncodedQuery.map { "?\($0)" } ?? "")
        let body = request.httpBody ?? Data()
        var lines = ["\(request.httpMethod ?? "GET") \(path) HTTP/1.0", "Host: \(host)"]
        for (name, value) in (request.allHTTPHeaderFields ?? [:]).sorted(by: { $0.key < $1.key })
        where !["host", "content-length", "connection"].contains(name.lowercased()) {
            lines.append("\(name): \(value)")
        }
        lines += ["Content-Length: \(body.count)", "Connection: close"]
        guard !lines.contains(where: { $0.contains("\r") || $0.contains("\n") }) else { return nil }
        return Data((lines.joined(separator: "\r\n") + "\r\n\r\n").utf8) + body
    }

    /// The status and body of an HTTP response, or nil if it isn't one.
    static func parse(_ data: Data) -> (Int, Data)? {
        guard let split = data.firstRange(of: Data("\r\n\r\n".utf8)) else { return nil }
        let head = String(decoding: data[..<split.lowerBound], as: UTF8.self).components(separatedBy: "\r\n")
        let statusParts = head.first?.split(separator: " ") ?? []
        guard statusParts.count >= 2, statusParts[0].hasPrefix("HTTP/"), let status = Int(statusParts[1])
        else { return nil }
        let body = Data(data[split.upperBound...])
        guard let length = head.dropFirst().first(where: { $0.lowercased().hasPrefix("content-length:") })
            .flatMap({ Int($0.dropFirst("content-length:".count).trimmingCharacters(in: .whitespaces)) })
        else { return (status, body) }
        // Cut short of its length, the reply isn't one: the caller reads that as no reply.
        guard length <= body.count else { return nil }
        return (status, body.prefix(length))
    }
}

/// One request and response on one connection. Everything runs on `queue`.
private final class PlainExchange: @unchecked Sendable {
    private let connection: NWConnection
    private let queue = DispatchQueue(label: "dev.ericdahl.phrasedeck.plain-http")
    private var continuation: CheckedContinuation<(Int, Data), any Error>?
    /// Set once bytes may have left: from then on a failure may have typed.
    private var sent = false
    private var received = Data()

    init(_ connection: NWConnection) { self.connection = connection }

    func run(_ payload: Data, timeout: TimeInterval) async throws -> (Int, Data) {
        try await withCheckedThrowingContinuation { continuation in
            queue.async { [self] in
                self.continuation = continuation
                connection.stateUpdateHandler = { [self] state in
                    switch state {
                    case .ready: send(payload)
                    // Before sending, nothing reached the helper.
                    case .waiting(let error) where HelperClient.stopsWaiting(error): fail()
                    case .failed: fail()
                    default: break
                    }
                }
                connection.start(queue: queue)
                queue.asyncAfter(deadline: .now() + timeout) { [self] in fail() }
            }
        }
    }

    private func send(_ payload: Data) {
        guard continuation != nil, !sent else { return }
        sent = true
        connection.send(content: payload, completion: .contentProcessed { [self] error in
            if error != nil { fail() } else { receive() }
        })
    }

    private func receive() {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [self] data, _, isComplete, error in
            if let data { received.append(data) }
            if received.count > HelperClient.maxReply {
                fail()
            } else if isComplete {
                if let reply = HelperClient.parse(received) { finish(.success(reply)) } else { fail() }
            } else if error != nil {
                fail()
            } else {
                receive()
            }
        }
    }

    private func fail() {
        finish(.failure(sent ? HelperClient.Failure.noReply : HelperClient.Failure.unreachable))
    }

    private func finish(_ result: Result<(Int, Data), any Error>) {
        guard let continuation else { return }
        self.continuation = nil
        connection.cancel()
        continuation.resume(with: result)
    }
}
