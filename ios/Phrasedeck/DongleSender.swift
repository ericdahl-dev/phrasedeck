import Foundation

/// A dongle the scan found. Not built yet.
struct FoundDongle: Hashable, Identifiable, Sendable {
    let id: UUID
    let name: String
}

@MainActor
protocol DongleLink: AnyObject {
    func scan() -> AsyncThrowingStream<[FoundDongle], any Error>
    func open(_ dongle: UUID) async throws -> any DongleChannel
}

@MainActor
protocol DongleChannel: AnyObject {
    var writeLength: Int { get }
    var tokenText: Data { get }
    var results: AsyncThrowingStream<Data, any Error> { get }
    func write(_ frame: Data) async throws
}

@MainActor
final class DongleSender {
    nonisolated static func defaultResultTimeout(_ bytes: Int) -> Duration { .zero }

    init(link: any DongleLink, resultTimeout: @escaping (Int) -> Duration = DongleSender.defaultResultTimeout) {}

    var isSending: Bool { false }
    func send(_ text: String, enter: Bool, to dongle: UUID, token: String?) async throws {}
    func stop() async {}
    func pair(_ dongle: UUID) async throws -> String { "" }
    func isReachable(_ dongle: UUID) async -> Bool { false }
    func scan() -> AsyncThrowingStream<[FoundDongle], any Error> { AsyncThrowingStream { $0.finish() } }
}
