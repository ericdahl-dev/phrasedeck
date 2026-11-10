import Foundation
import Observation

/// What the deck needs from a helper; `HelperClient` in the app, a stub
/// in tests and UI tests.
protocol HelperAPI: Sendable {
    func health() async throws -> Health
    func pair(code: String, device: String) async throws -> Pairing
    func info() async throws -> LANInfo
    func type(text: String, enter: Bool) async throws
}

extension HelperClient: HelperAPI {}

/// The app's state: computers on this device, their status dots, and the banner for the last
/// Send. The Courier carries each Send and status check (#206).
@MainActor
@Observable
final class AppModel {
    typealias Status = Courier.Status
    /// Which way the status check last reached a computer.
    typealias RouteKind = Courier.RouteKind

    struct Feedback: Equatable {
        enum Tone: Equatable {
            case success, amber, red

            /// What happened to the text, shown on the Button and leading every message.
            var word: String {
                switch self {
                case .success: "Typed"
                case .amber: "Maybe typed"
                case .red: "Not typed"
                }
            }

            /// SF Symbol shown with `word`.
            var symbol: String {
                switch self {
                case .success: "checkmark"
                case .amber: "questionmark"
                case .red: "xmark"
                }
            }
        }
        let tone: Tone
        let message: String?
        /// Set after a 401, so the banner can offer "Pair again".
        var pairAgain: Helper?
        /// Set when there's no Computer yet, so the banner can offer "Add".
        var addComputer = false

        /// The banner closes itself after a few seconds unless it asks for something.
        var autoCloses: Bool { pairAgain == nil && !addComputer }
    }

    let computers: Computers
    let courier: Courier
    private let device: String

    private(set) var statuses: [UUID: Status] = [:]
    private(set) var statusRoutes: [UUID: RouteKind] = [:]
    private(set) var feedback: Feedback?
    /// Counts banners shown, so the same message twice restarts its auto-close.
    private(set) var feedbackShown = 0

    var helpers: [Helper] { computers.all }

    var selectedHelperID: UUID? {
        get { computers.selectedID }
        set { computers.selectedID = newValue }
    }

    var selectedHelper: Helper? { computers.selected }

    init(computers: Computers, device: String) {
        self.computers = computers
        self.courier = Courier(computers: computers)
        self.device = device
    }

    // MARK: sending

    @discardableResult
    func send(text: String, enter: Bool) async -> Feedback {
        guard let helper = selectedHelper else {
            return show(Feedback(tone: .red, message: "Not typed: add a computer first.", addComputer: true))
        }
        let delivery = await courier.send(text, enter: enter, to: helper)
        return show(Outcome.send(delivery, on: helper))
    }

    /// The Computer a Bluetooth Send is typing on, which Stop can end (#202).
    var sendingTo: Helper? { courier.sending?.dongle == nil ? nil : courier.sending }

    /// Ends the Bluetooth Send in progress; its Outcome comes from the dongle's Result.
    func stop() async { await courier.stop() }

    func dismissFeedback() { feedback = nil }

    private func show(_ feedback: Feedback) -> Feedback {
        self.feedback = feedback
        feedbackShown += 1
        return feedback
    }

    // MARK: status dot

    func status(of helper: Helper) -> Status { statuses[helper.id] ?? .unknown }

    func statusRoute(of helper: Helper) -> RouteKind? { statusRoutes[helper.id] }

    /// Checks the Computer by the Route a Send would take (the Courier's check).
    func refreshStatus(of helper: Helper) async {
        let (status, route) = await courier.check(helper)
        statuses[helper.id] = status
        statusRoutes[helper.id] = route
    }

    // MARK: home network (#11)

    func setNearby(_ services: [NearbyService]) { computers.setNearby(services) }

    var nearby: [NearbyService] { computers.nearby }

    /// Nearby helpers that aren't paired on this device.
    var unpairedNearby: [NearbyService] {
        nearby.filter { service in !helpers.contains { service.matches($0) } }
    }

    // MARK: computers

    /// What a scanned QR code vouches for: the certificate fingerprint, and
    /// the LAN address and port to pair over.
    private struct QRTrust {
        let fingerprint: String
        let lan: String?
        let lanPort: Int
    }

    /// Pairs with a new computer by tailnet name and adds it only once
    /// pairing worked. Returns a message to show, or nil on success.
    func addHelper(host: String, port: Int?, code: String) async -> String? {
        let host = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !host.isEmpty else { return "Enter the computer's tailnet name." }
        // The typed route is plain HTTP with the token, so only tailnet hosts.
        guard PairLink.isTailnetRouteHost(host) else {
            return "Use the computer's tailnet name, or scan its QR code. A home network address or .local name isn't safe over plain HTTP."
        }
        return await addHelper(host: host, port: port, code: code, qr: nil)
    }

    /// Adds or re-pairs the computer a scanned QR code names. With LAN
    /// fields it pairs over pinned TLS to that address first.
    func addHelper(from link: PairLink) async -> String? {
        let qr = link.fingerprint.map { QRTrust(fingerprint: $0, lan: link.lan, lanPort: link.lanPort) }
        return await addHelper(host: link.host ?? "", port: link.port, code: link.code, qr: qr)
    }

    private func addHelper(host: String, port: Int?, code: String, qr: QRTrust?) async -> String? {
        let port = port ?? Helper.defaultPort
        // Scanning a computer that's already here pairs it again instead of adding a copy:
        // the same tailnet machine, or (without a host) the same certificate.
        let existing = helpers.first { helper in
            host.isEmpty ? helper.certSHA256 != nil && helper.certSHA256 == qr?.fingerprint
                : Helper.sameMachine(helper.host, host) && helper.port == port
        }
        if let existing {
            if let error = await pairAndStore(existing, code: code, qr: qr) { return error }
            selectedHelperID = existing.id
            return nil
        }
        var helper = Helper(name: host.isEmpty ? (qr?.lan ?? "") : Helper.displayName(for: host), host: host, port: port)
        helper.lanHost = host.isEmpty ? qr?.lan : nil
        let pairing: Pairing
        switch await pairForToken(helper, code: code, qr: qr) {
        case let .success(p): pairing = p
        case let .failure(message): return message
        }
        helper.certSHA256 = pairing.certSHA256 ?? qr?.fingerprint
        helper.lanPort = pairing.lanPort ?? qr?.lanPort
        do {
            try computers.add(helper)
        } catch {
            return "Couldn't save \(helper.name): \(error.localizedDescription)"
        }
        selectedHelperID = helper.id
        return nil
    }

    /// The dongles advertising the service nearby (#202).
    func scanForDongles() -> AsyncThrowingStream<[FoundDongle], any Error> { computers.dongles.scan() }

    /// Bonds with a dongle the scan found, keeps its Token and adds its Computer, or gives the
    /// one already here the new Token (after a BOOT reset). Returns a message, or nil on success.
    func addDongle(_ found: FoundDongle) async -> String? {
        let token: String
        do {
            token = try await computers.dongles.pair(found.id)
        } catch let failure as Dongle.Failure {
            return Outcome.pairDongle(failure, named: found.name)
        } catch {
            return "Couldn't pair with \(found.name): \(error.localizedDescription)"
        }
        let helper = helpers.first { $0.dongle == found.id } ?? Helper(name: found.name, host: "", dongle: found.id)
        do {
            try computers.setToken(token, for: helper)
            if !helpers.contains(where: { $0.id == helper.id }) { try computers.add(helper) }
        } catch {
            return "Couldn't save \(found.name): \(error.localizedDescription)"
        }
        selectedHelperID = helper.id
        return nil
    }

    /// Pairs an existing computer again, replacing its token.
    func pair(_ helper: Helper, code: String) async -> String? {
        let error = await pairAndStore(helper, code: code, qr: nil)
        if error == nil, feedback?.pairAgain?.id == helper.id { feedback = nil }
        return error
    }

    /// Removes the computer. Returns a message if it couldn't be (its Keychain token
    /// wouldn't delete), and then nothing changed.
    func delete(_ helper: Helper) -> String? {
        do {
            try computers.delete(helper)
        } catch {
            return "Couldn't remove \(helper.name): \(error.localizedDescription)"
        }
        statuses[helper.id] = nil
        statusRoutes[helper.id] = nil
        return nil
    }

    /// Pairs an existing computer and stores its new token and fingerprint.
    private func pairAndStore(_ helper: Helper, code: String, qr: QRTrust?) async -> String? {
        switch await pairForToken(helper, code: code, qr: qr) {
        case let .failure(message): return message
        case let .success(pairing):
            if let fp = pairing.certSHA256 ?? qr?.fingerprint {
                do {
                    try computers.learn(fp, lanPort: pairing.lanPort ?? qr?.lanPort, for: helper.id)
                } catch {
                    return Outcome.pinSaveFailed(on: helper, error)
                }
            }
            return nil
        }
    }

    private enum PairOutcome { case success(Pairing), failure(String) }

    /// Runs /pair (over pinned TLS to the QR code's LAN address first, if it
    /// has one) and saves the token. The code is single use, so the tailnet
    /// is tried only when the LAN connection was refused outright, and only
    /// for a computer with a tailnet name. A reply whose fingerprint isn't
    /// the QR code's is rejected.
    private func pairForToken(_ helper: Helper, code: String, qr: QRTrust?) async -> PairOutcome {
        let code = code.filter(\.isNumber)
        guard code.count == 6 else { return .failure(Outcome.badCode) }
        do {
            let pairing: Pairing
            if let qr, let lan = qr.lan {
                let route = HelperClient.Route.lan(host: lan, port: qr.lanPort, pin: qr.fingerprint)
                do {
                    pairing = try await computers.pairingConnection(to: helper, via: route).pair(code: code, device: device)
                } catch HelperClient.Failure.unreachable where helper.hasTailnetHost {
                    pairing = try await computers.pairingConnection(to: helper, via: .tailnet).pair(code: code, device: device)
                }
            } else {
                pairing = try await computers.pairingConnection(to: helper, via: .tailnet).pair(code: code, device: device)
            }
            if let qr, let fp = pairing.certSHA256, fp != qr.fingerprint {
                return .failure(Outcome.qrMismatch(on: helper))
            }
            try computers.setToken(pairing.token, for: helper)
            return .success(pairing)
        } catch let failure as HelperClient.Failure {
            return .failure(Outcome.pair(failure, on: helper))
        } catch {
            return .failure("Couldn't pair with \(helper.name): \(error.localizedDescription)")
        }
    }
}
