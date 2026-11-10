import Foundation

/// Carries a Send to a Computer by the right Route and brings back what happened to it (#206).
@MainActor
final class Courier {
    /// What happened to a Send: Typed, Maybe typed or Not typed, each with its reason.
    enum Delivery: Equatable {
        case typed(Reason?)
        case maybeTyped(Reason)
        case notTyped(Reason)
    }

    /// Why a Send ended as it did.
    enum Reason: Equatable {
        case helper(HelperClient.Failure)
        case dongle(Dongle.Failure)
    }

    private let computers: Computers

    init(computers: Computers) { self.computers = computers }

    func send(_ phrase: String, enter: Bool, to computer: Helper) async -> Delivery {
        do {
            try await computers.connection(to: computer, via: .tailnet).type(text: phrase, enter: enter)
            return .typed(nil)
        } catch let failure as HelperClient.Failure {
            return .notTyped(.helper(failure))
        } catch {
            return .notTyped(.helper(.unreachable))
        }
    }
}
