import Foundation
import Testing
@testable import Phrasedeck

/// Sending a Phrase and checking a Computer, on every Route, through the Courier (#206), with
/// fakes in place of the Helper and of Bluetooth.
@MainActor
@Suite(.serialized)
struct CourierTests {
    let defaults: UserDefaults
    let tokens = MemoryTokens()
    /// The tailnet route's Helper; `lan` answers on the Home network route.
    let api = FakeHelperAPI()
    let lan = FakeHelperAPI()
    let routes = RouteLog()
    let link = FakeDongleLink()
    let mac = Helper(name: "lib-2833", host: "lib-2833")

    init() throws {
        let suite = "CourierTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    /// Records each connection's route; the tailnet answers with `api`, the LAN with `lan`.
    func makeComputers() -> Computers {
        let (api, lan, routes) = (self.api, self.lan, self.routes)
        return Computers(defaults: defaults, tokens: tokens,
                         dongles: DongleSender(link: link, resultTimeout: { _ in .milliseconds(300) })) { _, _, route in
            routes.log.append(route)
            return route == .tailnet ? api : lan
        }
    }

    /// A Courier for this suite's Computers, with the Mac added and its Token stored.
    func courier() throws -> (Courier, Computers) {
        let computers = makeComputers()
        try computers.add(mac)
        try tokens.set("tok", for: mac.id)
        return (Courier(computers: computers), computers)
    }

    // MARK: the Tailnet route

    @Test func aSendOverTheTailnetIsTyped() async throws {
        let (courier, _) = try courier()

        let delivery = await courier.send("yes", enter: true, to: mac)

        #expect(delivery == .typed(nil))
        #expect(api.typed.count == 1)
        #expect(api.typed.first?.text == "yes")
        #expect(api.typed.first?.enter == true)
        #expect(routes.log == [.tailnet])
    }

    // MARK: the Home network route and the one tailnet fallback

    static let pin = String(repeating: "ab", count: 32)
    static let home = NearbyService(tailnetName: "lib-2833.tail1fdac.ts.net",
                                    address: LANAddress(host: "192.168.1.20", port: 47812))
    static let homeRoute = HelperClient.Route.lan(host: "192.168.1.20", port: 47812, pin: pin)

    /// The Mac with its Pin stored, seen nearby on the home network.
    func nearby() throws -> (Courier, Helper) {
        let (courier, computers) = try courier()
        try computers.learn(Self.pin, lanPort: 47812, for: mac.id)
        computers.setNearby([Self.home])
        return (courier, try #require(computers.all.first))
    }

    /// Paired with Tailscale off: reached only on the home network.
    func hostless(in computers: Computers) throws -> Helper {
        var hostless = Helper(name: "192.168.1.30", host: "")
        hostless.lanHost = "192.168.1.30"
        hostless.certSHA256 = Self.pin
        hostless.lanPort = 47900
        try computers.add(hostless)
        return hostless
    }

    @Test func aSendGoesOverTheHomeNetworkOnceWhenNearby() async throws {
        let (courier, pinned) = try nearby()

        #expect(await courier.send("yes", enter: true, to: pinned) == .typed(nil))
        #expect(lan.typed.count == 1 && api.typed.isEmpty)
        #expect(routes.log == [Self.homeRoute])
    }

    @Test func nearbyWithoutAPinStaysOnTheTailnet() async throws {
        let (courier, computers) = try courier()
        computers.setNearby([Self.home])

        _ = await courier.send("yes", enter: true, to: mac)

        #expect(routes.log == [.tailnet])
    }

    @Test func aDifferentNearbyComputerDoesNotCount() async throws {
        let (courier, computers) = try courier()
        try computers.learn(Self.pin, lanPort: 47812, for: mac.id)
        computers.setNearby([NearbyService(tailnetName: "omarchy-black2.tail1fdac.ts.net", address: Self.home.address)])

        _ = await courier.send("yes", enter: true, to: try #require(computers.all.first))

        #expect(routes.log == [.tailnet])
    }

    @Test func aMatchingAdvertisedFingerprintMakesAComputerNearby() async throws {
        let (courier, computers) = try courier()
        try computers.learn(Self.pin, lanPort: 47812, for: mac.id)
        computers.setNearby([NearbyService(tailnetName: nil, address: LANAddress(host: "192.168.1.44", port: 47812),
                                           advertisedFingerprint: Self.pin)])

        _ = await courier.send("yes", enter: true, to: try #require(computers.all.first))

        #expect(routes.log == [.lan(host: "192.168.1.44", port: 47812, pin: Self.pin)])
    }

    @Test func aDifferentAdvertisedFingerprintDoesNotMakeItNearby() async throws {
        let (courier, computers) = try courier()
        try computers.learn(Self.pin, lanPort: 47812, for: mac.id)
        computers.setNearby([NearbyService(tailnetName: nil, address: Self.home.address,
                                           advertisedFingerprint: String(repeating: "cd", count: 32))])

        _ = await courier.send("yes", enter: true, to: try #require(computers.all.first))

        #expect(routes.log == [.tailnet])
    }

    /// Its QR code's address, or where Bonjour sees it now.
    @Test func aComputerWithoutATailnetNameUsesItsHomeNetworkAddress() async throws {
        let computers = makeComputers()
        let hostless = try hostless(in: computers)
        let courier = Courier(computers: computers)

        _ = await courier.send("x", enter: false, to: hostless)
        computers.setNearby([NearbyService(tailnetName: nil, address: LANAddress(host: "192.168.1.77", port: 47900),
                                           advertisedFingerprint: Self.pin)])
        _ = await courier.send("x", enter: false, to: hostless)

        #expect(routes.log == [.lan(host: "192.168.1.30", port: 47900, pin: Self.pin),
                               .lan(host: "192.168.1.77", port: 47900, pin: Self.pin)])
    }

    @Test func anUnreachableHomeNetworkFallsBackToTheTailnetOnce() async throws {
        lan.typeError = .unreachable
        let (courier, pinned) = try nearby()

        #expect(await courier.send("yes", enter: true, to: pinned) == .typed(nil))
        #expect(lan.typed.count == 1 && api.typed.count == 1)
        #expect(routes.log == [Self.homeRoute, .tailnet])
    }

    /// Anything else may have reached the Helper, so the tailnet never gets a second try.
    @Test(arguments: [HelperClient.Failure.noReply, .failedPartway, .unauthorized, .busy, .server(502)])
    func otherHomeNetworkFailuresAreNotRetried(failure: HelperClient.Failure) async throws {
        lan.typeError = failure
        let (courier, pinned) = try nearby()

        #expect(await courier.send("yes", enter: true, to: pinned) != .typed(nil))
        #expect(lan.typed.count == 1 && api.typed.isEmpty)
    }

    /// Nothing reached the Helper on a Pin mismatch, so the tailnet gets its one try, and the
    /// result says what happened on the home network.
    @Test func aPinMismatchSendsOverTheTailnetAndSaysSo() async throws {
        lan.typeError = .pinMismatch
        let (courier, pinned) = try nearby()

        #expect(await courier.send("yes", enter: true, to: pinned) == .typed(.helper(.pinMismatch)))
        #expect(routes.log == [Self.homeRoute, .tailnet])
    }

    @Test func aPinMismatchWithTheTailnetDownIsNotTypedForThePin() async throws {
        lan.typeError = .pinMismatch
        api.typeError = .unreachable
        let (courier, pinned) = try nearby()

        #expect(await courier.send("yes", enter: true, to: pinned) == .notTyped(.helper(.pinMismatch)))
    }

    /// The tailnet try may have typed, and that matters more than the Pin.
    @Test func aPinMismatchThenNoReplyOverTheTailnetIsMaybeTyped() async throws {
        lan.typeError = .pinMismatch
        api.typeError = .noReply
        let (courier, pinned) = try nearby()

        #expect(await courier.send("yes", enter: true, to: pinned) == .maybeTyped(.helper(.noReply)))
    }

    @Test func aComputerWithoutATailnetNameIsNeverRetriedOnTheTailnet() async throws {
        lan.typeError = .unreachable
        let computers = makeComputers()
        let hostless = try hostless(in: computers)

        #expect(await Courier(computers: computers).send("x", enter: false, to: hostless)
                == .notTyped(.helper(.unreachable)))
        #expect(api.typed.isEmpty)
    }

    // MARK: errors the app doesn't recognize

    struct Unrecognized: LocalizedError {
        var errorDescription: String? { "something odd" }
    }

    /// The Send may have left, so it's never "safe to tap again".
    @Test func anUnrecognizedErrorIsMaybeTyped() async throws {
        api.typeOther = Unrecognized()
        let (courier, _) = try courier()

        #expect(await courier.send("yes", enter: true, to: mac) == .maybeTyped(.unrecognized("something odd")))
    }

    @Test func aCanceledSendToAHelperIsMaybeTyped() async throws {
        api.typeOther = CancellationError()
        let (courier, _) = try courier()

        #expect(await courier.send("yes", enter: true, to: mac).isMaybeTyped)
    }

    // MARK: the Bluetooth route

    let dongle = Helper(name: "Desk dongle", host: "", dongle: UUID())

    /// A Courier with the dongle's Computer added and its Token stored.
    func dongleCourier() throws -> Courier {
        let computers = makeComputers()
        try computers.add(dongle)
        try tokens.set(DongleFrameTests.tokenHex, for: dongle.id)
        return Courier(computers: computers)
    }

    @Test func aSendGoesOverBluetoothWithTheDonglesToken() async throws {
        let courier = try dongleCourier()

        #expect(await courier.send("hi", enter: true, to: dongle) == .typed(nil))
        let begin = try #require(link.writes.first)
        #expect(link.writes.count == 1)
        #expect(begin[2] == 0x01)  // Send Enter
        #expect(begin.dropFirst(5).prefix(16) == DongleFrameTests.token)
        #expect(api.typed.isEmpty && lan.typed.isEmpty && routes.log.isEmpty)
    }

    @Test func theDonglesResultIsTheReason() async throws {
        let courier = try dongleCourier()
        link.answer = { id in [Data([id, 0x03, 0xC3, 0xA9])] }

        #expect(await courier.send("é", enter: false, to: dongle) == .notTyped(.dongle(.result(.cantType("é")))))
    }

    @Test func aDongleOutOfRangeIsNotTyped() async throws {
        let courier = try dongleCourier()
        link.openError = .notInRange

        #expect(await courier.send("hi", enter: false, to: dongle) == .notTyped(.dongle(.notInRange)))
        #expect(link.writes.isEmpty)
    }

    @Test func withoutTheDonglesTokenNothingIsSent() async throws {
        let courier = try dongleCourier()
        try tokens.delete(dongle.id)

        #expect(await courier.send("hi", enter: false, to: dongle) == .notTyped(.dongle(.noToken)))
        #expect(link.writes.isEmpty)
    }

    @Test func aLinkLostMidSendIsMaybeTyped() async throws {
        let courier = try dongleCourier()
        link.dropAfterWrite = 0

        #expect(await courier.send("hi", enter: false, to: dongle) == .maybeTyped(.dongle(.lost)))
    }

    @Test func stopEndsTheBluetoothSendAsMaybeTyped() async throws {
        let courier = try dongleCourier()
        link.answer = { _ in [] }
        let send = Task { await courier.send("hi", enter: false, to: dongle) }
        try #require(await link.waitForBegin())
        #expect(courier.sending?.id == dongle.id)

        await courier.stop()

        #expect(await send.value == .maybeTyped(.dongle(.result(.stoppedPartway))))
        #expect(courier.sending == nil)
    }

    @Test func aCanceledBluetoothSendIsMaybeTyped() async throws {
        let courier = try dongleCourier()
        link.answer = { _ in [] }
        let send = Task { await courier.send("hi", enter: false, to: dongle) }
        try #require(await link.waitForBegin())

        send.cancel()

        #expect(await send.value.isMaybeTyped)
    }

    // MARK: one Send at a time

    @Test func aSecondTapWhileAHelperSendIsInFlightIsBusy() async throws {
        api.holdType = true
        let (courier, _) = try courier()
        let first = Task { await courier.send("one", enter: false, to: mac) }
        try #require(await api.waitForTyping())

        #expect(await courier.send("two", enter: false, to: mac) == .notTyped(.inFlight(mac)))

        api.finishTyping()
        #expect(await first.value == .typed(nil))
        #expect(api.typed.map(\.text) == ["one"])
    }

    /// Across Computers and Routes too: a Helper's Send waits for the dongle's to end.
    @Test func aTapForAnotherComputerWhileTheDongleTypesIsBusy() async throws {
        let computers = makeComputers()
        try computers.add(mac)
        try computers.add(dongle)
        try tokens.set(DongleFrameTests.tokenHex, for: dongle.id)
        let courier = Courier(computers: computers)
        link.answer = { _ in [] }
        let first = Task { await courier.send("hi", enter: false, to: dongle) }
        try #require(await link.waitForBegin())

        #expect(await courier.send("yes", enter: false, to: mac) == .notTyped(.inFlight(dongle)))
        #expect(api.typed.isEmpty)

        await courier.stop()
        _ = await first.value
        #expect(await courier.send("yes", enter: false, to: mac) == .typed(nil))
    }

    // MARK: checking a Computer

    /// What a check found, as one value to compare.
    struct Checked: Equatable {
        let status: Courier.Status
        let route: Courier.RouteKind?
        init(_ status: Courier.Status, _ route: Courier.RouteKind?) {
            self.status = status
            self.route = route
        }
    }

    func checked(_ courier: Courier, _ computer: Helper) async -> Checked {
        let (status, route) = await courier.check(computer)
        return Checked(status, route)
    }

    @Test func theCheckFollowsHealth() async throws {
        let (courier, _) = try courier()

        #expect(await checked(courier, mac) == Checked(.reachable, .tailnet))

        api.healthResult = .success(Health(hostname: "lib-2833", os: "darwin", accessibilityOK: false))
        #expect(await checked(courier, mac) == Checked(.notReady, .tailnet))

        api.healthResult = .failure(.unreachable)
        #expect(await checked(courier, mac) == Checked(.unreachable, nil))
    }

    @Test func theTailnetCheckLearnsThePinFromInfoOnce() async throws {
        api.infoResult = .success(LANInfo(certSHA256: Self.pin, lanPort: 47812))
        let (courier, computers) = try courier()

        _ = await courier.check(mac)
        _ = await courier.check(mac)

        #expect(computers.all.first?.certSHA256 == Self.pin)
        #expect(computers.all.first?.lanPort == 47812)
        #expect(api.infoCount == 1)
    }

    @Test func infoIsNeverAskedOverTheHomeNetwork() async throws {
        let (courier, pinned) = try nearby()

        #expect(await checked(courier, pinned) == Checked(.reachable, .home))
        #expect(lan.infoCount == 0 && api.infoCount == 0)
        #expect(lan.healthCount == 1 && api.healthCount == 0)
    }

    /// A check types nothing, so any home network failure gets the tailnet's try.
    @Test(arguments: [HelperClient.Failure.pinMismatch, .unreachable, .noReply])
    func theCheckFallsBackToTheTailnetWhenTheHomeNetworkFails(failure: HelperClient.Failure) async throws {
        lan.healthResult = .failure(failure)
        let (courier, pinned) = try nearby()

        #expect(await checked(courier, pinned) == Checked(.reachable, .tailnet))
        #expect(routes.log == [Self.homeRoute, .tailnet])
    }

    @Test func aComputerWithoutATailnetNameIsUnreachableWhenItsHomeNetworkFails() async throws {
        lan.healthResult = .failure(.unreachable)
        let computers = makeComputers()
        let hostless = try hostless(in: computers)

        #expect(await checked(Courier(computers: computers), hostless) == Checked(.unreachable, nil))
        #expect(api.healthCount == 0)
    }

    /// Bonjour's TXT record names a Computer but never vouches for its Pin.
    @Test func anAdvertisedFingerprintIsNeverLearned() async throws {
        let (courier, computers) = try courier()
        computers.setNearby([NearbyService(tailnetName: "lib-2833.tail1fdac.ts.net", address: Self.home.address,
                                           advertisedFingerprint: Self.pin)])

        #expect(await checked(courier, mac) == Checked(.reachable, .tailnet))
        #expect(computers.all.first?.certSHA256 == nil)
    }

    @Test func aDongleInRangeIsReachableOverBluetooth() async throws {
        let courier = try dongleCourier()

        #expect(await checked(courier, dongle) == Checked(.reachable, .bluetooth))
        #expect(api.healthCount == 0 && routes.log.isEmpty)
    }

    @Test func aDongleOutOfRangeIsUnreachable() async throws {
        let courier = try dongleCourier()
        link.openError = .notInRange

        #expect(await checked(courier, dongle) == Checked(.unreachable, .bluetooth))
    }
}

extension Courier.Delivery {
    var isMaybeTyped: Bool { if case .maybeTyped = self { true } else { false } }
}
