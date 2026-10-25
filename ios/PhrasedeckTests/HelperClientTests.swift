import Foundation
import Testing
import Synchronization
@testable import Phrasedeck

// Against the HTTP contract in docs/plans/2026-10-24-phrasedeck-design.md.
// Serialized because StubURLProtocol's reply is shared.
@Suite(.serialized)
struct HelperClientTests {
    let helper = Helper(name: "Mac", host: "erics-mac", port: 47811)

    /// erics-mac resolves to a tailnet address, as it does with Tailscale up.
    func client(token: String? = "tok-123") -> HelperClient {
        HelperClient(helper: helper, token: token, session: StubURLProtocol.session(),
                     resolve: { _ in ["100.101.102.103"] })
    }

    func json(_ data: Data?) throws -> [String: Any] {
        let data = try #require(data)
        return try #require(try JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    // MARK: routes

    @Test func theLANRouteUsesHTTPSToTheLANAddress() async throws {
        StubURLProtocol.reset(.status(200))
        let lan = HelperClient(helper: helper, token: "tok-123",
                               route: .lan(host: "192.168.1.20", port: 47812, pin: TestCerts.helperPin),
                               session: StubURLProtocol.session())

        try await lan.type(text: "yes", enter: true)

        let request = try #require(StubURLProtocol.requests.first?.request)
        #expect(request.url?.absoluteString == "https://192.168.1.20:47812/type")
        #expect(request.value(forHTTPHeaderField: "Authorization") == "Bearer tok-123")
    }

    /// With Tailscale off, a bare name can resolve through the router's DNS to a LAN address.
    /// Every address must be a tailnet one, or nothing (no token) is sent (#48 review).
    @Test(arguments: [["192.168.1.29"], ["100.101.102.103", "192.168.1.29"], ["fd00::29"], ["8.8.8.8"]])
    func aNameThatResolvesOffTheTailnetIsRefused(addresses: [String]) async {
        StubURLProtocol.reset(.status(200))
        let c = HelperClient(helper: helper, token: "tok", session: StubURLProtocol.session(),
                             resolve: { _ in addresses })

        await #expect(throws: HelperClient.Failure.notTailnet) { try await c.type(text: "x", enter: false) }
        await #expect(throws: HelperClient.Failure.notTailnet) { try await c.info() }
        #expect(StubURLProtocol.requests.isEmpty)
    }

    @Test func theSystemResolverReturnsNumericAddresses() async {
        let addresses = await HelperClient.systemResolve("localhost")
        #expect(addresses.contains("127.0.0.1"))
        #expect(await HelperClient.systemResolve("no-such-host.invalid").isEmpty)
    }

    @Test func aNameThatDoesNotResolveIsUnreachableAndSendsNothing() async {
        StubURLProtocol.reset(.status(200))
        let c = HelperClient(helper: helper, token: "tok", session: StubURLProtocol.session(), resolve: { _ in [] })

        await #expect(throws: HelperClient.Failure.unreachable) { try await c.health() }
        #expect(StubURLProtocol.requests.isEmpty)
    }

    /// The name is checked, then the request goes to the checked address, never a second
    /// lookup, with the name as Host (#71).
    @Test func theTokenGoesOnlyToTheCheckedAddress() async throws {
        StubURLProtocol.reset(.status(204))
        let c = HelperClient(helper: helper, token: "tok", session: StubURLProtocol.session(),
                             resolve: { name in
                                 #expect(name == "erics-mac")
                                 return ["fd7a:115c:a1e0::5", "100.70.1.5"]
                             })

        try await c.type(text: "x", enter: false)

        let request = try #require(StubURLProtocol.requests.first?.request)
        #expect(request.url?.absoluteString == "http://100.70.1.5:47811/type")
        #expect(request.value(forHTTPHeaderField: "Host") == "erics-mac:47811")
        #expect(request.value(forHTTPHeaderField: "Authorization") == "Bearer tok")
    }

    /// With MagicDNS off the QR code's host is a tailnet IP. It pairs: the request goes to that
    /// address through Network.framework (no ATS -1022), with no lookup (#71).
    @Test func anIPLiteralTailnetHostPairs() async throws {
        let sent = Mutex<[String]>([])
        let c = HelperClient(helper: Helper(name: "m", host: "100.101.102.103"), token: nil,
                             resolve: { _ in Issue.record("an IP literal was looked up"); return [] },
                             plainSend: { request, address, port in
                                 sent.withLock { $0.append("\(address):\(port) \(request.url?.host() ?? "")") }
                                 return (200, Data(#"{"token":"t"}"#.utf8))
                             })

        let pairing = try await c.pair(code: "123456", device: "iPhone")

        #expect(pairing.token == "t")
        #expect(sent.withLock { $0 } == ["100.101.102.103:47811 100.101.102.103"])
    }

    @Test(arguments: ["192.168.1.20", "fe80::1", "mac.local"])
    func theTailnetRouteNeverSendsPlainHTTPToALANHost(host: String) async {
        StubURLProtocol.reset(.status(200))
        let c = HelperClient(helper: Helper(name: "m", host: host), token: "tok", session: StubURLProtocol.session())

        await #expect(throws: HelperClient.Failure.notTailnet) { try await c.type(text: "x", enter: false) }
        #expect(StubURLProtocol.requests.isEmpty)
    }

    @Test func aPinMismatchIsItsOwnFailureNotNoReply() {
        #expect(HelperClient.failure(for: .cancelled, pinMismatched: true) == .pinMismatch) // spelling: ok (URLError.Code API)
        #expect(HelperClient.failure(for: .serverCertificateUntrusted, pinMismatched: true) == .pinMismatch)
        #expect(HelperClient.failure(for: .cancelled, pinMismatched: false) == .noReply) // spelling: ok (URLError.Code API)
    }

    // MARK: info and pairing fields

    @Test func infoReadsTheFingerprintAndLANPortWithAuth() async throws {
        StubURLProtocol.reset(.status(200, body: Data(#"{"certSHA256":"\#(TestCerts.helperPin)","lanPort":47812}"#.utf8)))

        let info = try await client().info()

        #expect(info == LANInfo(certSHA256: TestCerts.helperPin, lanPort: 47812))
        let request = try #require(StubURLProtocol.requests.first?.request)
        #expect(request.url?.absoluteString == "http://100.101.102.103:47811/info")
        #expect(request.httpMethod == "GET")
        #expect(request.value(forHTTPHeaderField: "Authorization") == "Bearer tok-123")
    }

    @Test func infoFromAnOldHelperIsEmpty() async throws {
        StubURLProtocol.reset(.status(404))
        #expect(try await client().info() == LANInfo(certSHA256: nil, lanPort: nil))
    }

    @Test func pairReturnsTheLANFieldsWhenTheHelperSendsThem() async throws {
        StubURLProtocol.reset(.status(200, body: Data(#"{"token":"t","certSHA256":"\#(TestCerts.helperPin.uppercased())","lanPort":47812}"#.utf8)))

        let pairing = try await client(token: nil).pair(code: "123456", device: "iPhone")

        #expect(pairing == Pairing(token: "t", certSHA256: TestCerts.helperPin, lanPort: 47812))
    }

    @Test func aBadFingerprintFromTheHelperIsDropped() async throws {
        StubURLProtocol.reset(.status(200, body: Data(#"{"token":"t","certSHA256":"nope","lanPort":0}"#.utf8)))

        let pairing = try await client(token: nil).pair(code: "123456", device: "iPhone")

        #expect(pairing == Pairing(token: "t", certSHA256: nil, lanPort: nil))
    }

    // MARK: type

    @Test func anIPv6HostIsBracketedInTheURL() async throws {
        StubURLProtocol.reset(.status(200, body: Data(#"{"hostname":"m","os":"darwin","accessibility_ok":true}"#.utf8)))
        let v6 = Helper(name: "m", host: "fd7a:115c:a1e0::1", port: 47811)

        _ = try await HelperClient(helper: v6, token: nil, session: StubURLProtocol.session()).health()

        #expect(StubURLProtocol.requests.first?.request.url?.absoluteString == "http://[fd7a:115c:a1e0::1]:47811/health")
    }

    @Test func typeSendsBearerJSONAndModeAutoOnce() async throws {
        StubURLProtocol.reset(.status(200))

        try await client().type(text: "yes", enter: true)

        let sent = StubURLProtocol.requests
        #expect(sent.count == 1)
        let request = try #require(sent.first?.request)
        #expect(request.httpMethod == "POST")
        #expect(request.url?.absoluteString == "http://100.101.102.103:47811/type")
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
        // Refused before anything was sent: never "may have typed".
        (.appTransportSecurityRequiresSecureConnection, .notSent(-1022)),
        (.unsupportedURL, .notSent(-1002)),
        (.notConnectedToInternet, .unreachable),
    ])
    func typeMapsTransportErrorsWithoutRetrying(code: URLError.Code, expected: HelperClient.Failure) async {
        StubURLProtocol.reset(.failure(code))

        await #expect(throws: expected) { try await client().type(text: "x", enter: false) }
        #expect(StubURLProtocol.requests.count == 1)
    }

    // MARK: pair

    @Test func pairPostsCodeAndDeviceWithoutAuthAndReturnsTheToken() async throws {
        StubURLProtocol.reset(.status(200, body: Data(#"{"token":"long-lived"}"#.utf8)))

        let pairing = try await client(token: nil).pair(code: "123456", device: "Eric's iPhone")

        #expect(pairing == Pairing(token: "long-lived", certSHA256: nil, lanPort: nil))
        let sent = StubURLProtocol.requests
        #expect(sent.count == 1)
        let request = try #require(sent.first?.request)
        #expect(request.url?.absoluteString == "http://100.101.102.103:47811/pair")
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
        #expect(request.url?.absoluteString == "http://100.101.102.103:47811/health")
        #expect(request.value(forHTTPHeaderField: "Authorization") == nil)
        #expect(StubURLProtocol.requests.count == 1)
    }

    @Test func healthMapsARefusedConnectionToUnreachable() async {
        StubURLProtocol.reset(.failure(.cannotConnectToHost))

        await #expect(throws: HelperClient.Failure.unreachable) { try await client().health() }
    }
}
