import Foundation
import Observation

/// Carries a Send to a Computer by the right Route and brings back what happened to it (#206).
@MainActor
@Observable
final class Courier {
    /// What happened to a Send: Typed, Maybe typed or Not typed, each with its reason.
    enum Delivery: Equatable {
        case typed(Reason?)
        case maybeTyped(Reason)
        case notTyped(Reason)

        /// What a Send that threw means. The one place an error becomes Typed, Maybe typed or
        /// Not typed.
        static func failed(_ error: any Error) -> Delivery {
            switch error {
            case let failure as HelperClient.Failure:
                switch failure {
                case .noReply, .failedPartway: return .maybeTyped(.helper(failure))
                default: return .notTyped(.helper(failure))
                }
            case let failure as Dongle.Failure:
                switch failure {
                // After the Begin: the link dropped, no Result came, or it stopped partway.
                case .lost, .noResult, .result(.stoppedPartway): return .maybeTyped(.dongle(failure))
                default: return .notTyped(.dongle(failure))
                }
            default:
                // Not one the Courier knows left nothing behind (a canceled task, say): the Send
                // may have gone out, so it's never "safe to tap again".
                return .maybeTyped(.unrecognized(error.localizedDescription))
            }
        }
    }

    /// Why a Send ended as it did.
    enum Reason: Equatable {
        case helper(HelperClient.Failure)
        case dongle(Dongle.Failure)
        /// An error neither route names, by its description.
        case unrecognized(String)
        /// Refused before sending: this Computer's Send was still in flight.
        case inFlight(Helper)
    }

    @ObservationIgnored private let computers: Computers
    /// The Computer a Send is on its way to, until its Delivery.
    private(set) var sending: Helper?

    init(computers: Computers) { self.computers = computers }

    /// One Send at a time, across every Computer and Route: a tap while one is in flight is
    /// refused, and nothing is sent.
    func send(_ phrase: String, enter: Bool, to computer: Helper) async -> Delivery {
        if let sending { return .notTyped(.inFlight(sending)) }
        sending = computer
        defer { sending = nil }
        if let dongle = computer.dongle {
            do {
                try await computers.dongles.send(phrase, enter: enter, to: dongle, token: computers.token(for: computer))
                return .typed(nil)
            } catch {
                return .failed(error)
            }
        }
        let attempt = await reach(computer, retryingIf: Self.leftNothing) {
            try await $0.type(text: phrase, enter: enter)
        }
        switch attempt.result {
        case .success:
            // Typed over the tailnet after the home network showed the wrong certificate: say so.
            return .typed(attempt.first == .pinMismatch ? .helper(.pinMismatch) : nil)
        case let .failure(error):
            let delivery = Delivery.failed(error)
            // A wrong certificate is what to fix, unless the tailnet try may have typed.
            if attempt.first == .pinMismatch, case .notTyped = delivery { return .notTyped(.helper(.pinMismatch)) }
            return delivery
        }
    }

    /// Ends the Bluetooth Send in progress; its Delivery comes from the dongle's Result.
    func stop() async { await computers.dongles.stop() }

    // MARK: routes

    /// The home network, pinned, when the Computer is Nearby and has a Pin from a trusted
    /// channel; otherwise the tailnet. The one place a Route is chosen.
    private func route(for computer: Helper) -> HelperClient.Route {
        guard let pin = computer.certSHA256 else { return .tailnet }
        if let address = computers.nearby.first(where: { $0.matches(computer) && $0.address != nil })?.address {
            return .lan(host: address.host, port: address.port, pin: pin)
        }
        // Paired with Tailscale off and not seen by Bonjour: the address from its QR code.
        if !computer.hasTailnetHost, let lanHost = computer.lanHost {
            return .lan(host: lanHost, port: computer.lanPort ?? PairLink.defaultLANPort, pin: pin)
        }
        return .tailnet
    }

    /// Refused, or a wrong certificate before the Token went out: nothing reached the Helper.
    private static func leftNothing(_ failure: HelperClient.Failure) -> Bool {
        failure == .unreachable || failure == .pinMismatch
    }

    /// One call to a Helper and how it went.
    private struct Attempt<Value> {
        let result: Result<Value, any Error>
        /// How the chosen Route failed, when the tailnet was tried after it.
        let first: HelperClient.Failure?
        /// The Route of the last try.
        let route: HelperClient.Route
    }

    /// The one tailnet fallback: `call` by the chosen Route, then once over the tailnet when the
    /// Route was the home network, it failed in a way `retryingIf` allows, and the Computer has
    /// a tailnet name.
    private func reach<Value>(_ computer: Helper, retryingIf retries: (HelperClient.Failure) -> Bool,
                              _ call: (any HelperAPI) async throws -> Value) async -> Attempt<Value> {
        let route = route(for: computer)
        do {
            return Attempt(result: .success(try await call(computers.connection(to: computer, via: route))),
                           first: nil, route: route)
        } catch let failure as HelperClient.Failure where route != .tailnet && computer.hasTailnetHost && retries(failure) {
            do {
                return Attempt(result: .success(try await call(computers.connection(to: computer, via: .tailnet))),
                               first: failure, route: .tailnet)
            } catch {
                return Attempt(result: .failure(error), first: failure, route: .tailnet)
            }
        } catch {
            return Attempt(result: .failure(error), first: nil, route: route)
        }
    }
}
