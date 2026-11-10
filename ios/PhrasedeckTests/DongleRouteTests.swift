import Foundation
import Testing
@testable import Phrasedeck

/// A Computer reached through a Phrasedeck dongle (#202): adding it, sending to it and its
/// status, through the app model, with a fake in place of Bluetooth.
@MainActor
@Suite(.serialized)
struct DongleRouteTests {
    let defaults: UserDefaults
    let tokens = MemoryTokens()
    let link = FakeDongleLink()
    let helperAPI = FakeHelperAPI()
    let found = FoundDongle(id: UUID(), name: "Phrasedeck dongle")

    init() throws {
        let suite = "DongleRouteTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    func makeComputers() -> Computers {
        let api = helperAPI
        return Computers(defaults: defaults, tokens: tokens,
                         dongles: DongleSender(link: link, resultTimeout: { _ in .milliseconds(300) })) { _, _, _ in api }
    }

    func model() -> AppModel { AppModel(computers: makeComputers(), device: "Eric's iPhone") }

    /// A model with the dongle already added and selected.
    func modelWithDongle() async throws -> AppModel {
        let model = model()
        #expect(await model.addDongle(found) == nil)
        return model
    }

    // MARK: adding

    @Test func addingADongleKeepsItsTokenAndSelectsIt() async throws {
        let model = model()

        #expect(await model.addDongle(found) == nil)

        let added = try #require(model.helpers.first)
        #expect(model.helpers.count == 1)
        #expect(added.dongle == found.id)
        #expect(added.name == "Phrasedeck dongle")
        #expect(!added.hasTailnetHost)
        #expect(model.selectedHelperID == added.id)
        #expect(try tokens.get(added.id) == DongleFrameTests.tokenHex)
    }

    @Test func theDongleComputerSurvivesAReload() async throws {
        _ = try await modelWithDongle()

        #expect(makeComputers().all.first?.dongle == found.id)
    }

    /// Past the 2 minutes after the bond the Token reads empty: nothing is added.
    @Test func withoutATokenNothingIsAdded() async throws {
        link.tokenText = Data()
        let model = model()

        let message = try #require(await model.addDongle(found))

        #expect(message.contains("2 minutes"))
        #expect(model.helpers.isEmpty)
    }

    @Test func aPairingThatDidNotFinishSaysSo() async throws {
        link.openError = .notPaired
        let model = model()

        let message = try #require(await model.addDongle(found))

        #expect(message.contains("passkey"))
        #expect(model.helpers.isEmpty)
    }

    /// Adding the same dongle again (after a BOOT reset) replaces its Token, not the Computer.
    @Test func addingTheSameDongleAgainKeepsOneComputer() async throws {
        let model = try await modelWithDongle()
        let other = "00112233445566778899aabbccddeeff"
        link.tokenText = Data(other.utf8)

        #expect(await model.addDongle(found) == nil)

        #expect(model.helpers.count == 1)
        #expect(try tokens.get(model.helpers[0].id) == other)
    }

    // MARK: sending

    @Test func aSendGoesOverBluetoothWithTheDonglesToken() async throws {
        let model = try await modelWithDongle()

        let feedback = await model.send(text: "hi", enter: true)

        #expect(feedback.tone == .success)
        let begin = try #require(link.writes.first)
        #expect(link.writes.count == 1)
        #expect(begin[2] == 0x01)  // Send Enter
        #expect(begin.dropFirst(5).prefix(16) == DongleFrameTests.token)
        #expect(helperAPI.typed.isEmpty)
    }

    @Test func theDonglesResultBecomesTheOutcome() async throws {
        let model = try await modelWithDongle()
        link.answer = { id in [Data([id, 0x03, 0xC3, 0xA9])] }

        let feedback = await model.send(text: "é", enter: false)

        #expect(feedback.tone == .red)
        #expect(feedback.message == "Not typed: a US keyboard can't type “é”.")
        #expect(model.feedback == feedback)
    }

    @Test func aDongleOutOfRangeIsNotTyped() async throws {
        let model = try await modelWithDongle()
        link.openError = .notInRange

        let feedback = await model.send(text: "hi", enter: false)

        #expect(feedback.tone == .red)
        #expect(link.writes.isEmpty)
    }

    @Test func aLinkLostMidSendIsMaybeTyped() async throws {
        let model = try await modelWithDongle()
        link.dropAfterWrite = 0

        #expect(await model.send(text: "hi", enter: false).tone == .amber)
    }

    @Test func stopEndsTheSendAsMaybeTyped() async throws {
        let model = try await modelWithDongle()
        link.answer = { _ in [] }
        let send = Task { await model.send(text: "hi", enter: false) }
        try #require(await link.waitForBegin())
        #expect(model.sendingTo?.dongle == found.id)

        await model.stop()

        let feedback = await send.value
        #expect(feedback.tone == .amber)
        #expect(feedback.message == "Maybe typed: Phrasedeck dongle stopped partway.")
        #expect(model.sendingTo == nil)
    }

    // MARK: status

    @Test func aDongleInRangeIsReachableOverBluetooth() async throws {
        let model = try await modelWithDongle()
        let dongle = try #require(model.selectedHelper)

        await model.refreshStatus(of: dongle)

        #expect(model.status(of: dongle) == .reachable)
        #expect(model.statusRoute(of: dongle) == .bluetooth)
        #expect(helperAPI.healthCount == 0)
    }

    @Test func aDongleOutOfRangeIsUnreachable() async throws {
        let model = try await modelWithDongle()
        let dongle = try #require(model.selectedHelper)
        link.openError = .notInRange

        await model.refreshStatus(of: dongle)

        #expect(model.status(of: dongle) == .unreachable)
        #expect(model.statusRoute(of: dongle) == .bluetooth)
    }

    @Test func removingADongleRemovesItsToken() async throws {
        let model = try await modelWithDongle()
        let dongle = try #require(model.selectedHelper)

        #expect(model.delete(dongle) == nil)

        #expect(model.helpers.isEmpty)
        #expect(try tokens.get(dongle.id) == nil)
    }
}

/// What each Bluetooth failure says, from the one module that words Outcomes (#44).
@MainActor
struct DongleOutcomeTests {
    let dongle = Helper(name: "Desk dongle", host: "", dongle: UUID())

    @Test(arguments: [
        (Dongle.Failure.bluetoothOff, AppModel.Feedback.Tone.red, "Not typed: Bluetooth is off."),
        (.bluetoothDenied, .red, "Not typed: allow Bluetooth for phrasedeck in Settings."),
        (.notInRange, .red, "Not typed: Desk dongle isn't in range."),
        (.notPaired, .red, "Not typed: Desk dongle isn't paired with this phone. Remove it and add it again."),
        (.noToken, .red, "Not typed: this phone has no Token for Desk dongle. Remove it and add it again."),
        (.writeTooShort(20), .red, "Not typed: Desk dongle's Bluetooth writes are too short (20 bytes)."),
        (.lost, .amber, "Maybe typed: the Bluetooth link to Desk dongle dropped."),
        (.noResult, .amber, "Maybe typed: no reply from Desk dongle."),
        (.result(.stoppedPartway), .amber, "Maybe typed: Desk dongle stopped partway."),
        (.result(.notStarted), .red, "Not typed: Desk dongle isn't plugged in to a Computer, or was stopped before the first key."),
        (.result(.cantType("é")), .red, "Not typed: a US keyboard can't type “é”."),
        (.result(.wrongToken), .red, "Not typed: Desk dongle refused this phone's Token. Remove it and add it again."),
        (.result(.busy), .red, "Not typed: Desk dongle is busy. Tap again."),
        (.result(.badFrame), .red, "Not typed: Desk dongle couldn't read the Send (error 6)."),
        (.result(.tooLong), .red, "Not typed: the Phrase is longer than 8192 bytes."),
        (.result(.unknown(0x42)), .red, "Not typed: Desk dongle failed (error 66)."),
    ])
    func eachFailureHasItsWording(failure: Dongle.Failure, tone: AppModel.Feedback.Tone, message: String) {
        let feedback = Outcome.send(failure, on: dongle)
        #expect(feedback.tone == tone)
        #expect(feedback.message == message)
        // Pair again is the Helper's sheet; a dongle is removed and added again instead.
        #expect(feedback.pairAgain == nil)
    }
}
