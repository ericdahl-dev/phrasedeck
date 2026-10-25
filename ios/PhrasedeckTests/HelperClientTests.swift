import Foundation
import Testing
@testable import Phrasedeck

// Against the HTTP contract in docs/plans/2026-10-24-phrasedeck-design.md.
// Serialized because StubURLProtocol's reply is shared.
@Suite(.serialized)
struct HelperClientTests {
    let helper = Helper(name: "Mac", host: "erics-mac", port: 47811)

    func client(token: String? = "tok-123") -> HelperClient {
        HelperClient(helper: helper, token: token, session: StubURLProtocol.session())
    }

    func json(_ data: Data?) throws -> [String: Any] {
        let data = try #require(data)
        return try #require(try JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    // MARK: type

    @Test func typeSendsBearerJSONAndModeAutoOnce() async throws {
        StubURLProtocol.reset(.status(200))

        try await client().type(text: "yes", enter: true)

        let sent = StubURLProtocol.requests
        #expect(sent.count == 1)
        let request = try #require(sent.first?.request)
        #expect(request.httpMethod == "POST")
        #expect(request.url?.absoluteString == "http://erics-mac:47811/type")
        #expect(request.value(forHTTPHeaderField: "Authorization") == "Bearer tok-123")
        #expect(request.value(forHTTPHeaderField: "Content-Type") == "application/json")
        #expect(request.timeoutInterval == 3)
        let body = try json(sent.first?.body)
        #expect(body["text"] as? String == "yes")
        #expect(body["enter"] as? Bool == true)
        #expect(body["mode"] as? String == "auto")
        #expect(body.count == 3)
    }

    @Test(arguments: [
        (409, HelperClient.Failure.noReply),
        (401, .unauthorized),
        (503, .notReady),
        (429, .rateLimited),
        (404, .server(404)),
        (500, .server(500)),
        (415, .server(415)),
    ])
    func typeMapsStatusCodes(status: Int, expected: HelperClient.Failure) async {
        StubURLProtocol.reset(.status(status))

        await #expect(throws: expected) { try await client().type(text: "x", enter: false) }
        #expect(StubURLProtocol.requests.count == 1)
    }

    @Test(arguments: [
        (URLError.Code.timedOut, HelperClient.Failure.noReply),
        (.networkConnectionLost, .noReply),
        (.cannotConnectToHost, .unreachable),
        (.cannotFindHost, .unreachable),
        (.dnsLookupFailed, .unreachable),
    ])
    func typeMapsTransportErrorsWithoutRetrying(code: URLError.Code, expected: HelperClient.Failure) async {
        StubURLProtocol.reset(.failure(code))

        await #expect(throws: expected) { try await client().type(text: "x", enter: false) }
        #expect(StubURLProtocol.requests.count == 1)
    }

    // MARK: pair

    @Test func pairPostsCodeAndDeviceWithoutAuthAndReturnsTheToken() async throws {
        StubURLProtocol.reset(.status(200, body: Data(#"{"token":"long-lived"}"#.utf8)))

        let token = try await client(token: nil).pair(code: "123456", device: "Eric's iPhone")

        #expect(token == "long-lived")
        let sent = StubURLProtocol.requests
        #expect(sent.count == 1)
        let request = try #require(sent.first?.request)
        #expect(request.url?.absoluteString == "http://erics-mac:47811/pair")
        #expect(request.httpMethod == "POST")
        #expect(request.value(forHTTPHeaderField: "Authorization") == nil)
        #expect(request.value(forHTTPHeaderField: "Content-Type") == "application/json")
        let body = try json(sent.first?.body)
        #expect(body["code"] as? String == "123456")
        #expect(body["device"] as? String == "Eric's iPhone")
    }

    @Test func pairMapsNoWindowAndWrongCode() async {
        StubURLProtocol.reset(.status(404))
        await #expect(throws: HelperClient.Failure.noPairingWindow) {
            try await client(token: nil).pair(code: "1", device: "d")
        }

        StubURLProtocol.reset(.status(401))
        await #expect(throws: HelperClient.Failure.unauthorized) {
            try await client(token: nil).pair(code: "1", device: "d")
        }
    }

    // MARK: health

    @Test func healthIsAnUnauthenticatedGet() async throws {
        let body = #"{"hostname":"erics-mac","os":"darwin","accessibility_ok":false}"#
        StubURLProtocol.reset(.status(200, body: Data(body.utf8)))

        let health = try await client().health()

        #expect(health == Health(hostname: "erics-mac", os: "darwin", accessibilityOK: false))
        let request = try #require(StubURLProtocol.requests.first?.request)
        #expect(request.httpMethod == "GET")
        #expect(request.url?.absoluteString == "http://erics-mac:47811/health")
        #expect(request.value(forHTTPHeaderField: "Authorization") == nil)
        #expect(StubURLProtocol.requests.count == 1)
    }

    @Test func healthMapsARefusedConnectionToUnreachable() async {
        StubURLProtocol.reset(.failure(.cannotConnectToHost))

        await #expect(throws: HelperClient.Failure.unreachable) { try await client().health() }
    }
}
