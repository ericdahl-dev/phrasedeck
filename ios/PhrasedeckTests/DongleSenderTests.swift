import Foundation
import Testing
@testable import Phrasedeck

/// Stands in for CoreBluetooth: records every frame, and answers a Send like the dongle does,
/// once all its bytes are in.
@MainActor
final class FakeDongleLink: DongleLink {
    var found: [FoundDongle] = []
    var openError: Dongle.Failure?
    var tokenText = Data(DongleFrameTests.tokenHex.utf8)
    var writeLength = 182
    /// What the dongle notifies once a Send's bytes are all in; empty answers nothing.
    var answer: (UInt8) -> [Data] = { [Data([$0, 0x00])] }
    /// What it notifies for a Stop.
    var stopAnswer: (UInt8) -> [Data] = { [Data([$0, 0x01])] }
    /// The write at this index fails with the error.
    var failWrite: (index: Int, error: Dongle.Failure)?
    /// The link drops right after the write at this index.
    var dropAfterWrite: Int?
    private(set) var opened = 0
    private(set) var writes: [Data] = []

    func scan() -> AsyncThrowingStream<[FoundDongle], any Error> {
        let found = found
        return AsyncThrowingStream { $0.yield(found) }
    }

    func open(_ dongle: UUID) async throws -> any DongleChannel {
        opened += 1
        if let openError { throw openError }
        return FakeDongleChannel(link: self)
    }

    /// Lets a Send started in a Task get its Begin out; false if it never does.
    func waitForBegin() async -> Bool {
        for _ in 0..<10_000 where writes.isEmpty { await Task.yield() }
        return !writes.isEmpty
    }

    fileprivate func record(_ frame: Data) throws -> Bool {
        if let failWrite, failWrite.index == writes.count { throw failWrite.error }
        writes.append(frame)
        return dropAfterWrite == writes.count - 1
    }
}

@MainActor
final class FakeDongleChannel: DongleChannel {
    let link: FakeDongleLink
    let results: AsyncThrowingStream<Data, any Error>
    private let notify: AsyncThrowingStream<Data, any Error>.Continuation
    private var expected = 0
    private var received = 0

    init(link: FakeDongleLink) {
        self.link = link
        (results, notify) = AsyncThrowingStream.makeStream()
    }

    var writeLength: Int { link.writeLength }
    var tokenText: Data { link.tokenText }

    func write(_ frame: Data) async throws {
        let drop = try link.record(frame)
        let bytes = [UInt8](frame)
        switch bytes.first {
        case 0x01:
            expected = Int(bytes[3]) | Int(bytes[4]) << 8
            received = bytes.count - 21
        case 0x02: received += bytes.count - 2
        case 0x03: link.stopAnswer(bytes[1]).forEach { notify.yield($0) }
        default: break
        }
        if drop { notify.finish(throwing: Dongle.Failure.lost); return }
        if bytes.first != 0x03, received == expected { link.answer(bytes[1]).forEach { notify.yield($0) } }
    }
}

@MainActor
struct DongleSenderTests {
    let link = FakeDongleLink()
    let dongle = UUID()
    let token = DongleFrameTests.tokenHex

    func sender() -> DongleSender { DongleSender(link: link, resultTimeout: { _ in .milliseconds(300) }) }

    @Test func aShortPhraseIsOneBeginAndTyped() async throws {
        try await sender().send("hi", enter: false, to: dongle, token: token)

        let begin = try #require(link.writes.first)
        #expect(link.writes.count == 1)
        #expect(begin.prefix(5) == Data([0x01, begin[1], 0x00, 0x02, 0x00]))
        #expect(begin.dropFirst(5).prefix(16) == DongleFrameTests.token)
    }

    @Test func aLongPhraseIsSplitToTheChannelsWriteLength() async throws {
        link.writeLength = 100
        try await sender().send(String(repeating: "a", count: 400), enter: true, to: dongle, token: token)

        #expect(link.writes.count == 5)
        #expect(link.writes.allSatisfy { $0.count <= 100 })
    }

    @Test func eachSendHasItsOwnID() async throws {
        let sender = sender()
        try await sender.send("a", enter: false, to: dongle, token: token)
        try await sender.send("b", enter: false, to: dongle, token: token)

        try #require(link.writes.count == 2)
        #expect(link.writes[0][1] != link.writes[1][1])
    }

    /// A Result for another Send doesn't answer this one.
    @Test func resultsForOtherSendsAreIgnored() async throws {
        link.answer = { id in [Data([id &+ 1, 0x04]), Data([id, 0x00])] }

        try await sender().send("hi", enter: false, to: dongle, token: token)
    }

    @Test(arguments: [
        (Data([0x01]), Dongle.Code.stoppedPartway),
        (Data([0x02]), .notStarted),
        (Data([0x03, 0xC3, 0xA9]), .cantType("é")),
        (Data([0x04]), .wrongToken),
        (Data([0x05]), .busy),
        (Data([0x06]), .badFrame),
        (Data([0x07]), .tooLong),
    ])
    func aResultOtherThanTypedIsThrown(rest: Data, code: Dongle.Code) async {
        link.answer = { id in [Data([id]) + rest] }

        await #expect(throws: Dongle.Failure.result(code)) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
    }

    // MARK: before the Begin: Not typed

    @Test func noConnectionIsNotInRangeAndNothingIsWritten() async {
        link.openError = .notInRange

        await #expect(throws: Dongle.Failure.notInRange) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
        #expect(link.writes.isEmpty)
    }

    @Test(arguments: [nil, "", "not hex"])
    func noTokenIsRefusedBeforeConnecting(token: String?) async {
        await #expect(throws: Dongle.Failure.noToken) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
        #expect(link.opened == 0)
    }

    @Test func aPhraseOver8192BytesIsRefusedBeforeConnecting() async {
        await #expect(throws: Dongle.Failure.result(.tooLong)) {
            try await sender().send(String(repeating: "a", count: 8193), enter: false, to: dongle, token: token)
        }
        #expect(link.opened == 0)
    }

    @Test func aWriteLengthTooShortForABeginSendsNothing() async {
        link.writeLength = 20

        await #expect(throws: Dongle.Failure.writeTooShort(20)) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
        #expect(link.writes.isEmpty)
    }

    /// The dongle refused the Begin write itself (not encrypted, say): nothing went in.
    @Test func aRefusedBeginIsNotPaired() async {
        link.failWrite = (0, .notPaired)

        await #expect(throws: Dongle.Failure.notPaired) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
    }

    // MARK: after the Begin: Maybe typed

    @Test func aLinkLostDuringTheBeginMayHaveTyped() async {
        link.failWrite = (0, .lost)

        await #expect(throws: Dongle.Failure.lost) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
    }

    @Test func aDataFrameThatFailsMayHaveTyped() async {
        link.failWrite = (1, .notPaired)

        await #expect(throws: Dongle.Failure.lost) {
            try await sender().send(String(repeating: "a", count: 400), enter: false, to: dongle, token: token)
        }
    }

    @Test func aLinkLostBeforeTheResultMayHaveTyped() async {
        link.dropAfterWrite = 0

        await #expect(throws: Dongle.Failure.lost) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
    }

    @Test func noResultInTimeMayHaveTyped() async {
        link.answer = { _ in [] }

        await #expect(throws: Dongle.Failure.noResult) {
            try await sender().send("hi", enter: false, to: dongle, token: token)
        }
    }

    /// The wait for a Result grows with the Phrase, since typing it takes longer.
    @Test func theResultWaitGrowsWithThePhrase() {
        #expect(DongleSender.defaultResultTimeout(8192) > DongleSender.defaultResultTimeout(2))
        #expect(DongleSender.defaultResultTimeout(0) >= .seconds(5))
    }

    // MARK: Stop and busy

    @Test func stopCancelsTheSendInProgress() async throws {
        link.answer = { _ in [] }
        let sender = DongleSender(link: link, resultTimeout: { _ in .seconds(10) })
        let send = Task { try await sender.send("hi", enter: false, to: dongle, token: token) }
        try #require(await link.waitForBegin())
        #expect(sender.isSending)

        await sender.stop()

        await #expect(throws: Dongle.Failure.result(.stoppedPartway)) { try await send.value }
        let begin = try #require(link.writes.first)
        #expect(link.writes.last == Dongle.stop(id: begin[1]))
        #expect(!sender.isSending)
    }

    @Test func stopWithNothingSendingWritesNothing() async {
        await sender().stop()
        #expect(link.writes.isEmpty)
    }

    /// One Send at a time from this phone: a second tap is busy, and nothing more is written.
    @Test func aSecondSendWhileOneIsTypingIsBusy() async throws {
        link.answer = { _ in [] }
        let sender = DongleSender(link: link, resultTimeout: { _ in .seconds(10) })
        let first = Task { try await sender.send("hi", enter: false, to: dongle, token: token) }
        try #require(await link.waitForBegin())

        await #expect(throws: Dongle.Failure.result(.busy)) {
            try await sender.send("again", enter: false, to: dongle, token: token)
        }
        #expect(link.opened == 1)
        await sender.stop()
        _ = try? await first.value
    }

    // MARK: pairing and status

    @Test func pairingReadsTheTokenAsHex() async throws {
        link.tokenText = Data(DongleFrameTests.tokenHex.uppercased().utf8)

        #expect(try await sender().pair(dongle) == DongleFrameTests.tokenHex)
    }

    /// Past its 2 minutes the Token reads empty.
    @Test func pairingWithoutATokenIsNoToken() async {
        link.tokenText = Data()

        await #expect(throws: Dongle.Failure.noToken) { try await sender().pair(dongle) }
    }

    @Test func pairingPassesOnWhyItCouldNotConnect() async {
        link.openError = .notPaired

        await #expect(throws: Dongle.Failure.notPaired) { try await sender().pair(dongle) }
    }

    @Test func aDongleIsReachableWhenItOpens() async {
        let sender = sender()
        #expect(await sender.isReachable(dongle))

        link.openError = .notInRange
        #expect(await !sender.isReachable(dongle))
    }

    /// A status check during a Send would take its Results: it doesn't open the link again.
    @Test func aStatusCheckDuringASendLeavesTheLinkAlone() async throws {
        link.answer = { _ in [] }
        let sender = DongleSender(link: link, resultTimeout: { _ in .seconds(10) })
        let send = Task { try await sender.send("hi", enter: false, to: dongle, token: token) }
        try #require(await link.waitForBegin())

        #expect(await sender.isReachable(dongle))
        #expect(link.opened == 1)
        await sender.stop()
        _ = try? await send.value
    }

    @Test func scanningPassesOnWhatTheLinkSees() async throws {
        let found = FoundDongle(id: UUID(), name: "Phrasedeck dongle")
        link.found = [found]

        var iterator = sender().scan().makeAsyncIterator()
        #expect(try await iterator.next() == [found])
    }
}
