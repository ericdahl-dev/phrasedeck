import Foundation

/// Answers every request from a handler and records what was sent, so
/// HelperClient tests never touch the network and can count requests.
final class StubURLProtocol: URLProtocol, @unchecked Sendable {
    enum Reply {
        case status(Int, body: Data = Data())
        case failure(URLError.Code)
    }

    private static let lock = NSLock()
    nonisolated(unsafe) private static var reply: Reply = .status(200)
    nonisolated(unsafe) private static var recorded: [(request: URLRequest, body: Data?)] = []

    static func reset(_ reply: Reply) {
        lock.withLock {
            self.reply = reply
            recorded = []
        }
    }

    static var requests: [(request: URLRequest, body: Data?)] { lock.withLock { recorded } }

    static func session() -> URLSession {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [StubURLProtocol.self]
        return URLSession(configuration: config)
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        let reply = Self.lock.withLock {
            Self.recorded.append((request, Self.readBody(request)))
            return Self.reply
        }
        switch reply {
        case let .status(code, body):
            let response = HTTPURLResponse(url: request.url!, statusCode: code, httpVersion: "HTTP/1.1",
                                           headerFields: ["Content-Type": "application/json"])!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: body)
            client?.urlProtocolDidFinishLoading(self)
        case let .failure(code):
            client?.urlProtocol(self, didFailWithError: URLError(code))
        }
    }

    override func stopLoading() {}

    // URLSession turns httpBody into a stream before a protocol sees it.
    private static func readBody(_ request: URLRequest) -> Data? {
        if let body = request.httpBody { return body }
        guard let stream = request.httpBodyStream else { return nil }
        stream.open()
        defer { stream.close() }
        var data = Data()
        var buffer = [UInt8](repeating: 0, count: 4096)
        while stream.hasBytesAvailable {
            let count = stream.read(&buffer, maxLength: buffer.count)
            guard count > 0 else { break }
            data.append(buffer, count: count)
        }
        return data
    }
}
