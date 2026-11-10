import Foundation

/// A dongle the scan found: its Bluetooth identifier and the name it advertises.
struct FoundDongle: Hashable, Identifiable, Sendable {
    let id: UUID
    let name: String
}

/// What the Bluetooth route needs from Bluetooth (#202): CoreBluetooth in the app
/// (`CoreBluetoothDongles`), a stub in UI tests and fakes in unit tests. It knows GATT, not frames.
@MainActor
protocol DongleLink: AnyObject {
    /// The dongles advertising the service, as they're seen. Iterating starts the scan; it stops
    /// when the iteration ends. Throws a `Dongle.Failure` when Bluetooth is off or not allowed.
    func scan() -> AsyncThrowingStream<[FoundDongle], any Error>
    /// Connects, or keeps the connection there is; subscribes to Result and reads the Token
    /// characteristic, which encrypts the link. The first time, that bonds, and iOS asks for the
    /// passkey. Throws a `Dongle.Failure` meaning nothing was sent.
    func open(_ dongle: UUID) async throws -> any DongleChannel
    /// Connects as `open` does, without a channel: the status check, which mustn't take a Send's
    /// Results.
    func connect(_ dongle: UUID) async throws
}

extension DongleLink {
    func connect(_ dongle: UUID) async throws { _ = try await open(dongle) }
}

/// One open link to a dongle, for one Send or one status check.
@MainActor
protocol DongleChannel: AnyObject {
    /// The longest write that's one frame: ATT MTU − 3 (182 on iOS).
    var writeLength: Int { get }
    /// The Token characteristic as `open` read it: 32 hex digits for 2 minutes after the bond.
    var tokenText: Data { get }
    /// Every Result notify from `open` on. Finishes throwing when the link drops.
    var results: AsyncThrowingStream<Data, any Error> { get }
    /// Writes one frame, with response. Throws `Dongle.Failure.notPaired` if the dongle refused
    /// it, `.notInRange` if there was no link to write it on, `.lost` if the link dropped after.
    func write(_ frame: Data) async throws
}

/// The Bluetooth route (#202): a Send to a dongle and its Outcome, Stop, pairing and the status
/// check, over a `DongleLink`. One Send at a time, and like the Helper's, never retried.
@MainActor
final class DongleSender {
    /// How long a Result may take: typing 8192 bytes takes several seconds, and the key interval
    /// may be slower than the dongle's default.
    nonisolated static func defaultResultTimeout(_ bytes: Int) -> Duration {
        .seconds(5) + .milliseconds(20) * bytes
    }

    private let link: any DongleLink
    private let resultTimeout: (Int) -> Duration
    private var nextID: UInt8 = 1
    /// True from a Send's start until its Outcome.
    private var busy = false
    /// The Send that Stop ends: its id and channel, once its Begin is out.
    private var current: (id: UInt8, channel: any DongleChannel)?
    private var stopped = false

    init(link: any DongleLink, resultTimeout: @escaping (Int) -> Duration = DongleSender.defaultResultTimeout) {
        self.link = link
        self.resultTimeout = resultTimeout
    }

    var isSending: Bool { busy }

    /// Types `text` through the dongle, with its Token (32 hex digits). Returns when it's Typed;
    /// throws a `Dongle.Failure` otherwise. Anything that fails before the Begin is Not typed;
    /// a lost link or no Result after it is Maybe typed.
    func send(_ text: String, enter: Bool, to dongle: UUID, token: String?) async throws {
        guard !busy else { throw Dongle.Failure.result(.busy) }
        guard let token = token.flatMap(Dongle.token(hex:)) else { throw Dongle.Failure.noToken }
        guard text.utf8.count <= Dongle.maxPhrase else { throw Dongle.Failure.result(.tooLong) }
        busy = true
        stopped = false
        defer { busy = false; current = nil }

        let channel = try await link.open(dongle)
        // Stopped while connecting: nothing was written.
        if stopped { throw Dongle.Failure.result(.notStarted) }
        let id = nextID
        nextID &+= 1
        let frames = try Dongle.frames(id: id, text: text, enter: enter, token: token, writeLength: channel.writeLength)
        // Subscribed before the Begin, so a fast Result isn't missed.
        let results = channel.results

        // Set before the Begin, so a Stop during its write follows it in the write queue.
        current = (id, channel)
        do {
            try await channel.write(frames[0])
        } catch let failure as Dongle.Failure where failure == .notPaired || failure == .notInRange {
            // Refused, or no link to write on: the Begin never went in.
            throw failure
        } catch {
            throw Dongle.Failure.lost
        }
        for frame in frames.dropFirst() where !stopped {
            do { try await channel.write(frame) } catch { throw Dongle.Failure.lost }
        }

        let result: Dongle.Result
        do {
            result = try await Self.result(for: id, in: results, within: resultTimeout(text.utf8.count))
        } catch let failure as Dongle.Failure {
            throw failure
        } catch {
            // Anything else after the Begin (a canceled task, say) may have typed too.
            throw Dongle.Failure.lost
        }
        if result.code != .typed { throw Dongle.Failure.result(result.code) }
    }

    /// Ends the Send in progress: the dongle answers its Result, stopped partway or before the
    /// first key. Nothing happens when nothing is sending.
    func stop() async {
        guard busy else { return }
        stopped = true
        guard let current else { return }
        try? await current.channel.write(Dongle.stop(id: current.id))
    }

    /// Bonds with the dongle (iOS asks for the passkey) and returns its Token as 32 hex digits.
    func pair(_ dongle: UUID) async throws -> String {
        let channel = try await link.open(dongle)
        guard let token = Dongle.tokenHex(fromCharacteristic: channel.tokenText) else { throw Dongle.Failure.noToken }
        return token
    }

    /// Whether the dongle is connected, or connects now. During a Send it is, and the Send's
    /// link is left alone.
    func isReachable(_ dongle: UUID) async -> Bool {
        if busy { return true }
        do {
            try await link.connect(dongle)
            return true
        } catch {
            return false
        }
    }

    func scan() -> AsyncThrowingStream<[FoundDongle], any Error> { link.scan() }

    /// The first Result for Send `id`; others are for earlier Sends. Lost when the link drops,
    /// no Result when it takes longer than `timeout`.
    private static func result(for id: UInt8, in results: AsyncThrowingStream<Data, any Error>,
                               within timeout: Duration) async throws -> Dongle.Result {
        try await withThrowingTaskGroup(of: Dongle.Result?.self) { group in
            group.addTask {
                do {
                    for try await data in results {
                        if let result = Dongle.result(data), result.id == id { return result }
                    }
                } catch {}
                throw Dongle.Failure.lost
            }
            group.addTask {
                try await Task.sleep(for: timeout)
                return nil
            }
            defer { group.cancelAll() }
            guard let first = try await group.next(), let result = first else { throw Dongle.Failure.noResult }
            return result
        }
    }
}
