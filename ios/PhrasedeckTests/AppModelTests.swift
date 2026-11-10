import Foundation
import Testing
@testable import Phrasedeck

/// Records every call and answers from a script, standing in for a helper.
final class FakeHelperAPI: HelperAPI, @unchecked Sendable {
    private let lock = NSLock()
    private var _typed: [(text: String, enter: Bool)] = []
    private var _paired: [(code: String, device: String)] = []
    var typeError: HelperClient.Failure?
    /// An error HelperClient never throws, for what the app doesn't recognize.
    var typeOther: (any Error)?
    var pairResult: Result<Pairing, HelperClient.Failure> = .success(Pairing(token: "new-token"))
    var infoResult: Result<LANInfo, HelperClient.Failure> = .success(LANInfo(certSHA256: nil, lanPort: nil))
    var healthCount = 0
    var infoCount = 0
    var healthResult: Result<Health, HelperClient.Failure> =
        .success(Health(hostname: "lib-2833", os: "darwin", accessibilityOK: true))

    var typed: [(text: String, enter: Bool)] { lock.withLock { _typed } }

    /// When set, `type` waits until `finishTyping()`, as a slow Send does.
    var holdType = false
    private var typing: CheckedContinuation<Void, Never>?

    /// Lets a Send started in a Task reach `type`; false if it never does.
    func waitForTyping() async -> Bool {
        for _ in 0..<10_000 where lock.withLock({ typing == nil }) { await Task.yield() }
        return lock.withLock { typing != nil }
    }

    func finishTyping() {
        let continuation = lock.withLock { () -> CheckedContinuation<Void, Never>? in
            defer { typing = nil }
            return typing
        }
        continuation?.resume()
    }
    var paired: [(code: String, device: String)] { lock.withLock { _paired } }

    func type(text: String, enter: Bool) async throws {
        lock.withLock { _typed.append((text, enter)) }
        if holdType { await withCheckedContinuation { continuation in lock.withLock { typing = continuation } } }
        if let typeError { throw typeError }
        if let typeOther { throw typeOther }
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
    /// No Keychain: tokens live in memory for the test's life (#43).
    let tokens = MemoryTokens()
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
        let computers = makeComputers()
        if withMac {
            try computers.add(mac)
            computers.selectedID = mac.id
            try tokens.set("tok", for: mac.id)
        }
        return AppModel(computers: computers, device: "Eric's iPhone")
    }

    /// Records each connection's route; the tailnet answers with `api`, the LAN with `lan`.
    func makeComputers() -> Computers {
        let (api, lan, routes) = (self.api, self.lan, self.routes)
        return Computers(defaults: defaults, tokens: tokens) { _, _, route in
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
        try model.computers.learn(Self.pin, lanPort: 47812, for: mac.id)
        // A fresh model reads the stored fingerprint back.
        let fresh = AppModel(computers: makeComputers(), device: "Eric's iPhone")
        fresh.setNearby([Self.home])
        return fresh
    }

    static let lanRoute = HelperClient.Route.lan(host: "192.168.1.20", port: 47812, pin: pin)

    // MARK: the banner (the Courier's own tests cover routes, fallback, busy and status: #206)

    /// With no Computer, a send says so and the banner offers Add, so it stays up (UX review).
    @Test func withNoComputerTheBannerOffersAdd() async throws {
        let model = try model(withMac: false)

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .red)
        #expect(feedback.addComputer)
        #expect(!feedback.autoCloses)
    }

    /// Typed over the tailnet after a pin mismatch: the banner says so and offers Pair again.
    @Test func aPinMismatchShowsAVisibleNote() async throws {
        lan.typeError = .pinMismatch
        let model = try nearbyModel()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .success)
        #expect(feedback.message?.hasPrefix("Typed: sent over the tailnet") == true)
        #expect(feedback.pairAgain?.id == model.selectedHelper?.id)
        #expect(model.feedback == feedback)
    }

    /// The bug #206 fixed: an error the app doesn't know shows Maybe typed, not "safe to tap again".
    @Test func anUnrecognizedErrorShowsMaybeTyped() async throws {
        api.typeOther = CancellationError()
        let model = try model()

        #expect(await model.send(text: "yes", enter: true).tone == .amber)
    }

    /// The other bug #206 fixed: a double tap on a Helper's Computer sends once.
    @Test func aSecondTapDuringASendShowsBusyAndSendsNothing() async throws {
        api.holdType = true
        let model = try model()
        let first = Task { await model.send(text: "one", enter: false) }
        try #require(await api.waitForTyping())

        let second = await model.send(text: "two", enter: false)

        #expect(second.message == "Not typed: lib-2833 is busy. Tap again.")
        // Stop is for a dongle's Send only.
        #expect(model.sendingTo == nil)
        api.finishTyping()
        #expect(await first.value.tone == .success)
        #expect(api.typed.count == 1)
    }

    @Test(arguments: ["192.168.1.20", "10.0.0.5", "fe80::1", "169.254.1.1", "8.8.8.8", "mac.local", "Mac.local."])
    func aTypedLANAddressIsRefusedSoTheTokenNeverGoesOverPlainHTTP(host: String) async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(host: host, port: nil, code: "123456")

        #expect(error?.contains("tailnet name") == true)
        #expect(api.paired.isEmpty && lan.paired.isEmpty && model.helpers.isEmpty)
    }

    @Test func unpairedNearbyListsOnlyComputersNotOnThisDevice() throws {
        let model = try nearbyModel()
        let other = NearbyService(tailnetName: "omarchy-black2.tail1fdac.ts.net", address: nil)
        let samePin = NearbyService(tailnetName: nil, address: nil, advertisedFingerprint: Self.pin)

        model.setNearby([Self.home, other, samePin])

        #expect(model.unpairedNearby == [other])
    }

    // MARK: fingerprint trust (#11)

    @Test func bonjourTXTCannotCarryAFingerprint() async throws {
        let txt = ["v": "1", "ts": "LIB-2833.tail1fdac.ts.net.", "port": "47812",
                   "fp": Self.pin, "certSHA256": Self.pin, "lan": "192.168.1.20"]
        let service = try #require(NearbyService(txt: txt, address: Self.home.address))
        // The TXT fp is kept only as a key for "nearby", never as a pin.
        #expect(service == NearbyService(tailnetName: "lib-2833.tail1fdac.ts.net", address: Self.home.address,
                                         advertisedFingerprint: Self.pin))

        let model = try model()
        model.setNearby([service])
        #expect(model.helpers.first?.certSHA256 == nil)
    }

    @Test(arguments: [["v": "1"], ["v": "2", "ts": "a.tail1.ts.net"], ["v": "1", "ts": "evil.example.com"],
                      ["v": "1", "fp": "nope"]])
    func badTXTIsIgnored(txt: [String: String]) {
        #expect(NearbyService(txt: txt, address: nil) == nil)
    }

    @Test func aTXTWithOnlyAFingerprintIsANearbyLANOnlyHelper() throws {
        let service = try #require(NearbyService(txt: ["v": "1", "fp": Self.pin.uppercased()], address: nil))
        #expect(service.tailnetName == nil && service.advertisedFingerprint == Self.pin)
    }

    // MARK: pairing from a QR code with LAN fields (#11)

    static let lanLink = PairLink(host: "omarchy-black2.tail1fdac.ts.net", port: 47811, code: "123456",
                                  lan: "192.168.1.30", fingerprint: pin)

    @Test func aLANLinkPairsOverPinnedTLSAndStoresTheFingerprint() async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(from: Self.lanLink)

        #expect(error == nil)
        let added = try #require(model.helpers.first)
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
        let route = HelperClient.Route.lan(host: "192.168.1.30", port: 47900, pin: Self.pin)
        #expect(routes.log == [route])
        #expect(added.lanHost == "192.168.1.30" && added.certSHA256 == Self.pin)

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
        #expect(added.certSHA256 == Self.pin)
        #expect(routes.log == [.tailnet])
    }

    @Test func aNewFingerprintDropsTheOldPinsSession() async throws {
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
        let model = try model()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .success)
        #expect(api.typed.count == 1)
        #expect(api.typed.first?.text == "yes")
        #expect(api.typed.first?.enter == true)
    }

    /// The wording itself is in OutcomeTests; here, a failed send shows it and is never retried.
    @Test(arguments: [HelperClient.Failure.noReply, .busy, .failedPartway, .unreachable, .notReady])
    func failuresShowTheirOutcomeWithoutRetrying(failure: HelperClient.Failure) async throws {
        api.typeError = failure
        let model = try model()

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback == Outcome.send(.failed(failure), on: mac))
        #expect(api.typed.count == 1)
        #expect(model.feedback == feedback)
    }

    @Test func unauthorizedOffersPairAgainForThatComputer() async throws {
        api.typeError = .unauthorized
        let model = try model()

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.pairAgain == mac)
    }

    @Test func withNoComputerNothingIsSent() async throws {
        let model = try model(withMac: false)

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.tone == .red)
        #expect(feedback.message == "Not typed: add a computer first.")
        #expect(api.typed.isEmpty)
    }

    // MARK: status dot

    @Test func theStatusDotShowsWhatTheCheckFound() async throws {
        let model = try model()

        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .reachable && model.statusRoute(of: mac) == .tailnet)

        api.healthResult = .success(Health(hostname: "lib-2833", os: "darwin", accessibilityOK: false))
        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .notReady)

        api.healthResult = .failure(.unreachable)
        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .unreachable && model.statusRoute(of: mac) == nil)
    }

    // MARK: adding, pairing, deleting

    @Test func addingAComputerPairsSavesTheTokenAndSelectsIt() async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(host: " omarchy-laptop ", port: nil, code: "123456")

        #expect(error == nil)
        let added = try #require(model.helpers.first)
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
        #expect(added.name == "lib-2833")
        #expect(added.host == "lib-2833.tail1fdac.ts.net")
        #expect(added.port == 47900)
    }

    @Test func addingAComputerThatIsAlreadyThereRepairsIt() async throws {
        let model = try model()

        let error = await model.addHelper(host: mac.host, port: mac.port, code: "123456")

        #expect(error == nil)
        #expect(model.helpers == [mac])
        #expect(model.selectedHelperID == mac.id)
        #expect(try tokens.get(mac.id) == "new-token")
    }

    @Test func scanningTheMagicDNSNameOfAComputerAddedByShortNameRepairsIt() async throws {
        let model = try model()

        let error = await model.addHelper(host: "lib-2833.tail1fdac.ts.net", port: mac.port, code: "123456")

        #expect(error == nil)
        #expect(model.helpers.map(\.id) == [mac.id])
        #expect(try tokens.get(mac.id) == "new-token")
    }

    @Test func aMissingPairingWindowSaysToRunPhrasedPairFirst() async throws {
        api.pairResult = .failure(.notFound)
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop", port: 47811, code: "123456")

        #expect(error == "Run phrased pair on omarchy-laptop first.")
        #expect(model.helpers.isEmpty)
    }

    /// Pairing never types, so a lost reply must not say "may have typed" (P0 2026-10-25).
    @Test func noReplyWhilePairingSaysToTryAgainNotMayHaveTyped() async throws {
        api.pairResult = .failure(.noReply)
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop.tail1fdac.ts.net", port: nil, code: "123456")

        #expect(error == "omarchy-laptop didn't answer. Run phrased pair again and retry.")
    }

    @Test func aBlockedConnectionWhilePairingSaysNothingWasSent() async throws {
        api.pairResult = .failure(.notSent(-1022))
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop.tail1fdac.ts.net", port: nil, code: "123456")

        #expect(error == "omarchy-laptop: the connection was blocked before anything was sent (error -1022).")
    }

    @Test func aWrongCodeSaysSo() async throws {
        api.pairResult = .failure(.unauthorized)
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop", port: nil, code: "000000")

        #expect(error == "That code didn't match. Check it, or run phrased pair again.")
        #expect(model.helpers.isEmpty)
    }

    @Test func pairAgainReplacesTheToken() async throws {
        let model = try model()

        let error = await model.pair(mac, code: "654321")

        #expect(error == nil)
        #expect(try tokens.get(mac.id) == "new-token")
        #expect(model.helpers == [mac])
    }

    @Test func deletingAComputerClearsItsTokenAndSelection() async throws {
        let model = try model()

        #expect(model.delete(mac) == nil)

        #expect(model.helpers.isEmpty)
        #expect(model.selectedHelperID == nil)
        #expect(try tokens.get(mac.id) == nil)
        #expect(makeComputers().all.isEmpty)
    }

    /// A Keychain failure is shown, and nothing is removed (#43).
    @Test func aFailedDeleteIsReportedAndKeepsTheComputer() async throws {
        let model = try model()
        tokens.failDelete = CocoaError(.fileWriteNoPermission)

        let message = try #require(model.delete(mac))

        #expect(message.contains("lib-2833"))
        #expect(model.helpers == [mac])
        #expect(model.selectedHelperID == mac.id)
        #expect(try tokens.get(mac.id) == "tok")
    }

    @Test func deletingTheSelectedComputerSelectsTheNextOne() async throws {
        let model = try model()
        let laptop = Helper(name: "omarchy", host: "omarchy-laptop.tail1fdac.ts.net")
        try model.computers.add(laptop)

        #expect(model.delete(mac) == nil)

        #expect(model.selectedHelperID == laptop.id)
    }
}
