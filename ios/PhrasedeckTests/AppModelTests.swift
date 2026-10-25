import Foundation
import Testing
@testable import Phrasedeck

/// Records every call and answers from a script, standing in for a helper.
final class FakeHelperAPI: HelperAPI, @unchecked Sendable {
    private let lock = NSLock()
    private var _typed: [(text: String, enter: Bool)] = []
    private var _paired: [(code: String, device: String)] = []
    var typeError: HelperClient.Failure?
    var pairResult: Result<Pairing, HelperClient.Failure> = .success(Pairing(token: "new-token"))
    var infoResult: Result<LANInfo, HelperClient.Failure> = .success(LANInfo(certSHA256: nil, lanPort: nil))
    var healthCount = 0
    var infoCount = 0
    var healthResult: Result<Health, HelperClient.Failure> =
        .success(Health(hostname: "lib-2833", os: "darwin", accessibilityOK: true))

    var typed: [(text: String, enter: Bool)] { lock.withLock { _typed } }
    var paired: [(code: String, device: String)] { lock.withLock { _paired } }

    func type(text: String, enter: Bool) async throws {
        lock.withLock { _typed.append((text, enter)) }
        if let typeError { throw typeError }
    }

    func pair(code: String, device: String) async throws -> Pairing {
        lock.withLock { _paired.append((code, device)) }
        return try pairResult.get()
    }

    func health() async throws -> Health {
        lock.withLock { healthCount += 1 }
        return try healthResult.get()
    }

    func info() async throws -> LANInfo {
        lock.withLock { infoCount += 1 }
        return try infoResult.get()
    }
}

final class RouteLog: @unchecked Sendable {
    var log: [HelperClient.Route] = []
}

@MainActor
@Suite(.serialized)
struct AppModelTests {
    let defaults: UserDefaults
    let tokens = TokenStore(service: "dev.ericdahl.phrasedeck.tests")
    /// The tailnet route's helper; `lan` answers on the LAN route.
    let api = FakeHelperAPI()
    let lan = FakeHelperAPI()
    let routes = RouteLog()
    let mac = Helper(name: "lib-2833", host: "lib-2833")

    init() throws {
        let suite = "AppModelTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    func model(withMac: Bool = true) throws -> AppModel {
        let store = HelperStore(defaults: defaults, tokens: tokens)
        if withMac {
            try store.add(mac)
            store.selectedHelperID = mac.id
            try tokens.set("tok", for: mac.id)
        }
        let (api, lan, routes) = (self.api, self.lan, self.routes)
        return AppModel(store: store, tokens: tokens, device: "Eric's iPhone") { _, _, route in
            routes.log.append(route)
            return route == .tailnet ? api : lan
        }
    }

    static let pin = String(repeating: "ab", count: 32)
    static let home = NearbyService(tailnetName: "lib-2833.tail1fdac.ts.net",
                                    address: LANAddress(host: "192.168.1.20", port: 47812))

    /// The Mac with a stored fingerprint, seen nearby on the LAN.
    func nearbyModel() throws -> AppModel {
        let model = try model()
        var pinned = mac
        pinned.certSHA256 = Self.pin
        pinned.lanPort = 47812
        try HelperStore(defaults: defaults, tokens: tokens).update(pinned)
        let fresh = AppModel(store: HelperStore(defaults: defaults, tokens: tokens), tokens: tokens,
                             device: "Eric's iPhone") { [api, lan, routes] _, _, route in
            routes.log.append(route)
            return route == .tailnet ? api : lan
        }
        _ = model
        fresh.setNearby([Self.home])
        return fresh
    }

    static let lanRoute = HelperClient.Route.lan(host: "192.168.1.20", port: 47812, pin: pin)

    // MARK: route choice (#11)

    @Test func withNothingNearbyTheTailnetIsUsed() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()
        #expect(model.route(for: mac) == .tailnet)
    }

    @Test func nearbyWithoutAFingerprintStaysOnTheTailnet() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()
        model.setNearby([Self.home])
        #expect(model.route(for: mac) == .tailnet)
    }

    @Test func nearbyWithAFingerprintUsesTheLANAddressWithThePin() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try nearbyModel()
        #expect(model.route(for: try #require(model.selectedHelper)) == Self.lanRoute)
    }

    @Test func aDifferentNearbyComputerDoesNotCount() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try nearbyModel()
        model.setNearby([NearbyService(tailnetName: "omarchy-black2.tail1fdac.ts.net", address: Self.home.address)])
        #expect(model.route(for: try #require(model.selectedHelper)) == .tailnet)
    }

    @Test func aSendGoesOverTheLANOnceWhenNearby() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try nearbyModel()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .success)
        #expect(lan.typed.count == 1 && api.typed.isEmpty)
        #expect(routes.log == [Self.lanRoute])
    }

    @Test func anUnreachableLANFallsBackToTheTailnetOnce() async throws {
        defer { try? tokens.delete(mac.id) }
        lan.typeError = .unreachable
        let model = try nearbyModel()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .success)
        #expect(lan.typed.count == 1 && api.typed.count == 1)
        #expect(routes.log == [Self.lanRoute, .tailnet])
    }

    @Test(arguments: [HelperClient.Failure.noReply, .unauthorized, .server(500)])
    func otherLANFailuresAreNotRetriedOnTheTailnet(failure: HelperClient.Failure) async throws {
        defer { try? tokens.delete(mac.id) }
        lan.typeError = failure
        let model = try nearbyModel()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone != .success)
        #expect(lan.typed.count == 1 && api.typed.isEmpty)
    }

    /// Nothing reached the helper on a pin mismatch, so the tailnet is safe
    /// to try once; the note says what happened, so it's never silent.
    @Test func aPinMismatchSendsOverTheTailnetWithAVisibleNote() async throws {
        defer { try? tokens.delete(mac.id) }
        lan.typeError = .pinMismatch
        let model = try nearbyModel()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .success)
        #expect(feedback.message?.contains("certificate") == true)
        #expect(lan.typed.count == 1 && api.typed.count == 1)
        #expect(routes.log == [Self.lanRoute, .tailnet])
    }

    @Test func aPinMismatchWithTheTailnetDownIsARedErrorThatSaysNothingWasSent() async throws {
        defer { try? tokens.delete(mac.id) }
        lan.typeError = .pinMismatch
        api.typeError = .unreachable
        let model = try nearbyModel()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .red)
        #expect(feedback.message?.contains("nothing was sent") == true)
    }

    @Test(arguments: ["192.168.1.20", "10.0.0.5", "fe80::1", "169.254.1.1", "8.8.8.8", "mac.local", "Mac.local."])
    func aTypedLANAddressIsRefusedSoTheTokenNeverGoesOverPlainHTTP(host: String) async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(host: host, port: nil, code: "123456")

        #expect(error?.contains("tailnet name") == true)
        #expect(api.paired.isEmpty && lan.paired.isEmpty && model.helpers.isEmpty)
    }

    @Test func unpairedNearbyListsOnlyComputersNotOnThisDevice() throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()
        let other = NearbyService(tailnetName: "omarchy-black2.tail1fdac.ts.net", address: nil)

        model.setNearby([Self.home, other])

        #expect(model.unpairedNearby == [other])
    }

    // MARK: fingerprint trust (#11)

    @Test func bonjourTXTCannotCarryAFingerprint() throws {
        defer { try? tokens.delete(mac.id) }
        let txt = ["v": "1", "ts": "LIB-2833.tail1fdac.ts.net.", "port": "47812",
                   "fp": Self.pin, "certSHA256": Self.pin, "lan": "192.168.1.20"]
        let service = try #require(NearbyService(txt: txt, address: Self.home.address))
        #expect(service == NearbyService(tailnetName: "lib-2833.tail1fdac.ts.net", address: Self.home.address))

        let model = try model()
        model.setNearby([service])
        #expect(model.helpers.first?.certSHA256 == nil)
        #expect(model.route(for: mac) == .tailnet)
    }

    @Test(arguments: [["v": "1"], ["v": "2", "ts": "a.tail1.ts.net"], ["v": "1", "ts": "evil.example.com"]])
    func badTXTIsIgnored(txt: [String: String]) {
        #expect(NearbyService(txt: txt, address: nil) == nil)
    }

    @Test func theTailnetStatusCheckLearnsTheFingerprintFromInfo() async throws {
        defer { try? tokens.delete(mac.id) }
        api.infoResult = .success(LANInfo(certSHA256: Self.pin, lanPort: 47812))
        let model = try model()

        await model.refreshStatus(of: mac)
        await model.refreshStatus(of: mac)

        #expect(model.helpers.first?.certSHA256 == Self.pin)
        #expect(model.helpers.first?.lanPort == 47812)
        #expect(api.infoCount == 1)
        #expect(model.statusRoute(of: mac) == .tailnet)
    }

    @Test func infoIsNeverAskedOverTheLAN() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try nearbyModel()

        await model.refreshStatus(of: try #require(model.selectedHelper))

        #expect(lan.infoCount == 0 && api.infoCount == 0)
        #expect(lan.healthCount == 1)
        #expect(model.statusRoute(of: try #require(model.selectedHelper)) == .home)
    }

    @Test func theStatusCheckFallsBackToTheTailnetWhenTheLANFails() async throws {
        defer { try? tokens.delete(mac.id) }
        lan.healthResult = .failure(.pinMismatch)
        let model = try nearbyModel()
        let helper = try #require(model.selectedHelper)

        await model.refreshStatus(of: helper)

        #expect(model.status(of: helper) == .reachable)
        #expect(model.statusRoute(of: helper) == .tailnet)
    }

    // MARK: pairing from a QR code with LAN fields (#11)

    static let lanLink = PairLink(host: "omarchy-black2.tail1fdac.ts.net", port: 47811, code: "123456",
                                  lan: "192.168.1.30", fingerprint: pin)

    @Test func aLANLinkPairsOverPinnedTLSAndStoresTheFingerprint() async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(from: Self.lanLink)

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        #expect(routes.log == [.lan(host: "192.168.1.30", port: 47812, pin: Self.pin)])
        #expect(lan.paired.count == 1 && api.paired.isEmpty)
        #expect(added.certSHA256 == Self.pin)
        #expect(added.lanPort == 47812)
        #expect(added.host == "omarchy-black2.tail1fdac.ts.net")
    }

    @Test func aLANLinkFallsBackToTheTailnetWhenTheLANIsUnreachable() async throws {
        lan.pairResult = .failure(.unreachable)
        let model = try model(withMac: false)

        let error = await model.addHelper(from: Self.lanLink)

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        #expect(lan.paired.count == 1 && api.paired.count == 1)
        #expect(added.certSHA256 == Self.pin)
    }

    @Test func aLANLinkThatGetsNoReplyIsNotRetriedBecauseTheCodeIsSingleUse() async throws {
        lan.pairResult = .failure(.noReply)
        let model = try model(withMac: false)

        let error = await model.addHelper(from: Self.lanLink)

        #expect(error != nil)
        #expect(lan.paired.count == 1 && api.paired.isEmpty)
        #expect(model.helpers.isEmpty)
    }

    static let hostlessLink = PairLink(host: nil, port: 47811, code: "123456",
                                       lan: "192.168.1.30", fingerprint: pin, lanPort: 47900)

    @Test func aHostlessLinkPairsOnlyOverTheLANAndAlwaysSendsThere() async throws {
        lan.pairResult = .success(Pairing(token: "t", certSHA256: Self.pin, lanPort: 47900))
        let model = try model(withMac: false)

        let error = await model.addHelper(from: Self.hostlessLink)

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        let route = HelperClient.Route.lan(host: "192.168.1.30", port: 47900, pin: Self.pin)
        #expect(routes.log == [route])
        #expect(added.lanHost == "192.168.1.30" && added.certSHA256 == Self.pin)
        #expect(model.route(for: added) == route)

        _ = await model.send(text: "x", enter: false)
        #expect(routes.log.last == route)
    }

    @Test func aHostlessLinkIsNotRetriedOnTheTailnet() async throws {
        lan.pairResult = .failure(.unreachable)
        let model = try model(withMac: false)

        let error = await model.addHelper(from: Self.hostlessLink)

        #expect(error != nil)
        #expect(api.paired.isEmpty && model.helpers.isEmpty)
    }

    @Test func aHostlessComputerWhoseLANFailsIsNotRetriedOnTheTailnet() async throws {
        lan.pairResult = .success(Pairing(token: "t", certSHA256: Self.pin, lanPort: 47900))
        let model = try model(withMac: false)
        _ = await model.addHelper(from: Self.hostlessLink)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        lan.typeError = .unreachable

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.tone == .red)
        #expect(api.typed.isEmpty)
    }

    @Test func aQRPairingWhoseResponseFingerprintDiffersIsRejected() async throws {
        api.pairResult = .success(Pairing(token: "t", certSHA256: String(repeating: "cd", count: 32), lanPort: 47812))
        lan.pairResult = .failure(.unreachable)
        let model = try model(withMac: false)

        let error = await model.addHelper(from: Self.lanLink)

        #expect(error?.contains("QR code") == true)
        #expect(model.helpers.isEmpty)
    }

    @Test func aTypedPairingStoresTheFingerprintFromTheResponse() async throws {
        api.pairResult = .success(Pairing(token: "t", certSHA256: Self.pin, lanPort: 47812))
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-black2", port: nil, code: "123456")

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        #expect(added.certSHA256 == Self.pin)
        #expect(routes.log == [.tailnet])
    }

    @Test func aNewFingerprintDropsTheOldPinsSession() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try nearbyModel()
        let old = HelperClient(helper: mac, token: nil, route: Self.lanRoute).session
        let newPin = String(repeating: "cd", count: 32)
        api.pairResult = .success(Pairing(token: "t2", certSHA256: newPin, lanPort: 47812))

        let error = await model.pair(try #require(model.selectedHelper), code: "123456")

        #expect(error == nil)
        #expect(model.selectedHelper?.certSHA256 == newPin)
        #expect(HelperClient(helper: mac, token: nil, route: Self.lanRoute).session !== old)
    }

    // MARK: sending

    @Test func aTapSendsOnceAndFlashesGreen() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .success)
        #expect(api.typed.count == 1)
        #expect(api.typed.first?.text == "yes")
        #expect(api.typed.first?.enter == true)
    }

    @Test(arguments: [
        (HelperClient.Failure.noReply, AppModel.Feedback.Tone.amber, "no reply, it may have typed"),
        (.unreachable, .red, "lib-2833 unreachable"),
        (.unauthorized, .red, "lib-2833 refused this device. Pair again."),
        (.notReady, .red, "lib-2833 can't type yet: grant Accessibility to phrased"),
        (.rateLimited, .red, "Too many sends. Wait a moment."),
        (.server(500), .red, "lib-2833 failed (error 500)"),
    ])
    func failuresMapToTheirFeedbackWithoutRetrying(failure: HelperClient.Failure, tone: AppModel.Feedback.Tone,
                                                   message: String) async throws {
        defer { try? tokens.delete(mac.id) }
        api.typeError = failure
        let model = try model()

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.tone == tone)
        #expect(feedback.message == message)
        #expect(api.typed.count == 1)
        #expect(model.feedback == feedback)
    }

    @Test func unauthorizedOffersPairAgainForThatComputer() async throws {
        defer { try? tokens.delete(mac.id) }
        api.typeError = .unauthorized
        let model = try model()

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.pairAgain == mac)
    }

    @Test func withNoComputerNothingIsSent() async throws {
        let model = try model(withMac: false)

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.tone == .red)
        #expect(feedback.message == "Add a computer first.")
        #expect(api.typed.isEmpty)
    }

    // MARK: status dot

    @Test func statusFollowsHealth() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .reachable)

        api.healthResult = .success(Health(hostname: "lib-2833", os: "darwin", accessibilityOK: false))
        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .notReady)

        api.healthResult = .failure(.unreachable)
        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .unreachable)
    }

    // MARK: adding, pairing, deleting

    @Test func addingAComputerPairsSavesTheTokenAndSelectsIt() async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(host: " omarchy-laptop ", port: nil, code: "123456")

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        #expect(added.host == "omarchy-laptop")
        #expect(added.name == "omarchy-laptop")
        #expect(added.port == Helper.defaultPort)
        #expect(model.selectedHelperID == added.id)
        #expect(try tokens.get(added.id) == "new-token")
        #expect(api.paired.first?.code == "123456")
        #expect(api.paired.first?.device == "Eric's iPhone")
    }

    @Test func aMagicDNSNameIsShownByItsShortName() async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "lib-2833.tail1fdac.ts.net", port: 47900, code: "123456")

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        #expect(added.name == "lib-2833")
        #expect(added.host == "lib-2833.tail1fdac.ts.net")
        #expect(added.port == 47900)
    }

    @Test func addingAComputerThatIsAlreadyThereRepairsIt() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        let error = await model.addHelper(host: mac.host, port: mac.port, code: "123456")

        #expect(error == nil)
        #expect(model.helpers == [mac])
        #expect(model.selectedHelperID == mac.id)
        #expect(try tokens.get(mac.id) == "new-token")
    }

    @Test func scanningTheMagicDNSNameOfAComputerAddedByShortNameRepairsIt() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        let error = await model.addHelper(host: "lib-2833.tail1fdac.ts.net", port: mac.port, code: "123456")

        #expect(error == nil)
        #expect(model.helpers.map(\.id) == [mac.id])
        #expect(try tokens.get(mac.id) == "new-token")
    }

    @Test func aMissingPairingWindowSaysToRunPhrasedPairFirst() async throws {
        api.pairResult = .failure(.noPairingWindow)
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop", port: 47811, code: "123456")

        #expect(error == "Run phrased pair on omarchy-laptop first.")
        #expect(model.helpers.isEmpty)
    }

    @Test func aWrongCodeSaysSo() async throws {
        api.pairResult = .failure(.unauthorized)
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop", port: nil, code: "000000")

        #expect(error == "That code didn't match. Check it, or run phrased pair again.")
        #expect(model.helpers.isEmpty)
    }

    @Test func pairAgainReplacesTheToken() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        let error = await model.pair(mac, code: "654321")

        #expect(error == nil)
        #expect(try tokens.get(mac.id) == "new-token")
        #expect(model.helpers == [mac])
    }

    @Test func deletingAComputerClearsItsTokenAndSelection() async throws {
        let model = try model()

        model.delete(mac)

        #expect(model.helpers.isEmpty)
        #expect(model.selectedHelperID == nil)
        #expect(try tokens.get(mac.id) == nil)
    }
}
