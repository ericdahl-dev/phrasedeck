import Foundation

/// For UI tests (with -uiTestingStubHelper): a dongle in place of Bluetooth (#202). The scan finds
/// one, pairing reads a Token, and each Send is Typed and counted. With -uiTestingStubSlowDongle a
/// Send types until Stop, which ends it stopped partway.
@MainActor
final class StubDongle: DongleLink {
    static let id = UUID(uuidString: "6B1D0000-0000-4000-8000-000000000001")!

    let probe: StubHelper.Probe
    let slow = ProcessInfo.processInfo.arguments.contains("-uiTestingStubSlowDongle")

    init(probe: StubHelper.Probe) { self.probe = probe }

    func scan() -> AsyncThrowingStream<[FoundDongle], any Error> {
        AsyncThrowingStream { $0.yield([FoundDongle(id: Self.id, name: Dongle.name)]) }
    }

    func open(_ dongle: UUID) async throws -> any DongleChannel { Channel(probe: probe, slow: slow) }

    @MainActor
    final class Channel: DongleChannel {
        let writeLength = 182
        let tokenText = Data(String(repeating: "5f", count: 16).utf8)
        let results: AsyncThrowingStream<Data, any Error>
        private let notify: AsyncThrowingStream<Data, any Error>.Continuation
        private let probe: StubHelper.Probe
        private let slow: Bool

        init(probe: StubHelper.Probe, slow: Bool) {
            (results, notify) = AsyncThrowingStream.makeStream()
            self.probe = probe
            self.slow = slow
        }

        func write(_ frame: Data) async throws {
            let bytes = [UInt8](frame)
            guard bytes.count >= 2 else { return }
            switch bytes[0] {
            case 0x01:
                probe.sendCount += 1
                if !slow { notify.yield(Data([bytes[1], 0x00])) }
            case 0x03:
                notify.yield(Data([bytes[1], 0x01]))
            default:
                break
            }
        }
    }
}
