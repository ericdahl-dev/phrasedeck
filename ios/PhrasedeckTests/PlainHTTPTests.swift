import Foundation
import Network
import Testing
@testable import Phrasedeck

/// One-shot HTTP server on 127.0.0.1: records the raw request and answers `reply`.
final class OneShotServer: @unchecked Sendable {
    let listener: NWListener
    private let lock = NSLock()
    private var received = Data()
    var request: String { lock.withLock { String(decoding: received, as: UTF8.self) } }
    var port: Int { Int(listener.port?.rawValue ?? 0) }

    init(reply: String) async throws {
        let params = NWParameters.tcp
        params.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: .any)
        listener = try NWListener(using: params)
        listener.newConnectionHandler = { [self] connection in
            connection.start(queue: .global())
            read(connection, reply: Data(reply.utf8))
        }
        await withCheckedContinuation { (done: CheckedContinuation<Void, Never>) in
            listener.stateUpdateHandler = { if case .ready = $0 { done.resume() } }
            listener.start(queue: .global())
        }
    }

    /// Reads until the headers and any Content-Length body are in, then answers and closes.
    private func read(_ connection: NWConnection, reply: Data) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [self] data, _, _, error in
            if let data { lock.withLock { received.append(data) } }
            let text = request
            if let split = text.range(of: "\r\n\r\n") {
                let length = text[..<split.lowerBound].split(separator: "\r\n")
                    .first { $0.lowercased().hasPrefix("content-length:") }
                    .flatMap { Int($0.split(separator: ":")[1].trimmingCharacters(in: .whitespaces)) } ?? 0
                if text.utf8.count - text[..<split.upperBound].utf8.count >= length {
                    connection.send(content: reply, completion: .contentProcessed { _ in connection.cancel() })
                    return
                }
            }
            if error == nil { read(connection, reply: reply) }
        }
    }

    deinit { listener.cancel() }
}

struct PlainHTTPTests {
    /// The tailnet route's sender connects to the address it's given, never resolving the URL's
    /// host, and Network.framework isn't subject to ATS, so a bare 100.x host works (#71).
    @Test func itSendsToTheGivenAddressWithTheURLHostAsHost() async throws {
        let server = try await OneShotServer(
            reply: "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\n\r\n{\"token\":\"abc\"}")
        var request = URLRequest(url: URL(string: "http://100.101.102.103:47811/pair")!)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue("Bearer tok", forHTTPHeaderField: "Authorization")
        request.httpBody = Data(#"{"code":"123456"}"#.utf8)

        let (status, body) = try await HelperClient.networkSend(request, "127.0.0.1", server.port)

        #expect(status == 200)
        #expect(String(decoding: body, as: UTF8.self) == #"{"token":"abc"}"#)
        let sent = server.request
        #expect(sent.hasPrefix("POST /pair HTTP/1.0\r\n"))
        #expect(sent.contains("\r\nHost: 100.101.102.103:47811\r\n"))
        #expect(sent.contains("\r\nAuthorization: Bearer tok\r\n"))
        #expect(sent.contains("\r\nContent-Length: 17\r\n"))
        #expect(sent.hasSuffix("\r\n\r\n{\"code\":\"123456\"}"))
    }

    @Test func aRefusedConnectionIsUnreachable() async throws {
        let server = try await OneShotServer(reply: "")
        let port = server.port
        server.listener.cancel()
        try await Task.sleep(for: .milliseconds(100))
        let request = URLRequest(url: URL(string: "http://100.101.102.103:47811/health")!)

        await #expect(throws: HelperClient.Failure.unreachable) {
            _ = try await HelperClient.networkSend(request, "127.0.0.1", port)
        }
    }
}
