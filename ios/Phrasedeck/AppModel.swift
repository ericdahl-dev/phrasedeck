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

/// The app's state: computers on this device, their status dots, and the
/// result of the last send. Sends once and never retries.
@MainActor
@Observable
final class AppModel {
    enum Status: Equatable { case unknown, reachable, notReady, unreachable }
    /// Which way the status check last reached a computer.
    enum RouteKind: Equatable { case home, tailnet }

    struct Feedback: Equatable {
        enum Tone: Equatable { case success, amber, red }
        let tone: Tone
        let message: String?
        /// Set after a 401, so the banner can offer "Pair again".
        var pairAgain: Helper?
    }

    typealias MakeClient = @MainActor (Helper, String?, HelperClient.Route) -> any HelperAPI

    private let store: HelperStore
    private let tokens: TokenStore
    private let device: String
    private let makeClient: MakeClient

    private(set) var helpers: [Helper]
    private(set) var statuses: [UUID: Status] = [:]
    private(set) var statusRoutes: [UUID: RouteKind] = [:]
    /// Phrasedeck helpers Bonjour sees on the home network.
    private(set) var nearby: [NearbyService] = []
    /// Computers already asked for /info this run.
    private var infoAsked: Set<UUID> = []
    private(set) var feedback: Feedback?

    var selectedHelperID: UUID? {
        didSet { store.selectedHelperID = selectedHelperID }
    }

    var selectedHelper: Helper? { helpers.first { $0.id == selectedHelperID } }

    init(store: HelperStore, tokens: TokenStore, device: String, makeClient: @escaping MakeClient) {
        self.store = store
        self.tokens = tokens
        self.device = device
        self.makeClient = makeClient
        helpers = store.helpers
        selectedHelperID = store.selectedHelper?.id
    }

    // MARK: sending

    @discardableResult
    func send(text: String, enter: Bool) async -> Feedback {
        guard let helper = selectedHelper else {
            return show(Feedback(tone: .red, message: "Add a computer first."))
        }
        do {
            // One attempt per route: the tailnet only when the LAN refused the connection,
            // so nothing reached the helper; a LAN timeout may have typed.
            let route = route(for: helper)
            do {
                try await client(for: helper, route).type(text: text, enter: enter)
            } catch HelperClient.Failure.unreachable where route != .tailnet {
                try await client(for: helper, .tailnet).type(text: text, enter: enter)
            }
            return show(Feedback(tone: .success, message: nil))
        } catch let failure as HelperClient.Failure {
            return show(Self.feedback(for: failure, on: helper))
        } catch {
            return show(Feedback(tone: .red, message: "\(helper.name): \(error.localizedDescription)"))
        }
    }

    func dismissFeedback() { feedback = nil }

    static func feedback(for failure: HelperClient.Failure, on helper: Helper) -> Feedback {
        let name = helper.name
        switch failure {
        case .noReply: return Feedback(tone: .amber, message: "no reply, it may have typed")
        case .unreachable: return Feedback(tone: .red, message: "\(name) unreachable")
        case .unauthorized:
            return Feedback(tone: .red, message: "\(name) refused this device. Pair again.", pairAgain: helper)
        case .notReady: return Feedback(tone: .red, message: "\(name) can't type yet: grant Accessibility to phrased")
        case .rateLimited: return Feedback(tone: .red, message: "Too many sends. Wait a moment.")
        case .noPairingWindow: return Feedback(tone: .red, message: "Run phrased pair on \(name) first.")
        case let .server(code): return Feedback(tone: .red, message: "\(name) failed (error \(code))")
        case .pinMismatch:
            return Feedback(tone: .red, message: "\(name)'s certificate didn't match, so nothing was sent. Scan its QR code to pair again.")
        }
    }

    private func show(_ feedback: Feedback) -> Feedback {
        self.feedback = feedback
        return feedback
    }

    // MARK: status dot

    func status(of helper: Helper) -> Status { statuses[helper.id] ?? .unknown }

    func statusRoute(of helper: Helper) -> RouteKind? { statusRoutes[helper.id] }

    /// Checks the route a send would take, then the tailnet if that fails.
    /// Over the tailnet it also learns the LAN fingerprint from /info once.
    func refreshStatus(of helper: Helper) async {
        let route = route(for: helper)
        if route != .tailnet, let health = try? await client(for: helper, route).health() {
            statuses[helper.id] = health.accessibilityOK ? .reachable : .notReady
            statusRoutes[helper.id] = .home
            return
        }
        do {
            let health = try await client(for: helper, .tailnet).health()
            statuses[helper.id] = health.accessibilityOK ? .reachable : .notReady
            statusRoutes[helper.id] = .tailnet
        } catch {
            statuses[helper.id] = .unreachable
            statusRoutes[helper.id] = nil
            return
        }
        guard helper.certSHA256 == nil, !infoAsked.contains(helper.id) else { return }
        infoAsked.insert(helper.id)
        if let info = try? await client(for: helper, .tailnet).info(), let fp = info.certSHA256 {
            learn(fp, lanPort: info.lanPort, for: helper.id)
        }
    }

    // MARK: home network (#11)

    func setNearby(_ services: [NearbyService]) { nearby = services }

    /// Nearby helpers that aren't paired on this device.
    var unpairedNearby: [NearbyService] {
        nearby.filter { service in !helpers.contains { Helper.sameMachine($0.host, service.tailnetName) } }
    }

    /// The LAN, pinned, when the computer is nearby and has a fingerprint
    /// from a trusted channel; otherwise the tailnet.
    func route(for helper: Helper) -> HelperClient.Route {
        guard let pin = helper.certSHA256,
              let address = nearby.first(where: { Helper.sameMachine(helper.host, $0.tailnetName) })?.address
        else { return .tailnet }
        return .lan(host: address.host, port: address.port, pin: pin)
    }

    /// Stores a fingerprint from /pair, the QR code or /info. A changed one
    /// drops the old pin's connections.
    private func learn(_ fingerprint: String, lanPort: Int?, for id: UUID) {
        guard var helper = helpers.first(where: { $0.id == id }) else { return }
        if let old = helper.certSHA256, old != fingerprint { HelperClient.forgetLANSession(pin: old) }
        helper.certSHA256 = fingerprint
        helper.lanPort = lanPort ?? helper.lanPort
        try? store.update(helper)
        helpers = store.helpers
    }

    // MARK: computers

    /// Pairs with a new computer by tailnet name and adds it only once
    /// pairing worked. Returns a message to show, or nil on success.
    func addHelper(host: String, port: Int?, code: String) async -> String? {
        await addHelper(host: host, port: port, code: code, lan: nil)
    }

    /// Adds or re-pairs the computer a scanned QR code names. With LAN
    /// fields it pairs over pinned TLS to that address first.
    func addHelper(from link: PairLink) async -> String? {
        let lan = link.lan.flatMap { address in link.fingerprint.map { (address, $0) } }
        return await addHelper(host: link.host, port: link.port, code: link.code, lan: lan)
    }

    private func addHelper(host: String, port: Int?, code: String, lan: (address: String, pin: String)?) async -> String? {
        let host = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !host.isEmpty else { return "Enter the computer's tailnet name." }
        let port = port ?? Helper.defaultPort
        // Scanning a computer that's already here pairs it again instead of adding a copy.
        if let existing = helpers.first(where: { Helper.sameMachine($0.host, host) && $0.port == port }) {
            if let error = await pairAndStore(existing, code: code, lan: lan) { return error }
            selectedHelperID = existing.id
            return nil
        }
        var helper = Helper(name: Helper.displayName(for: host), host: host, port: port)
        let pairing: Pairing
        switch await pairForToken(helper, code: code, lan: lan) {
        case let .success(p): pairing = p
        case let .failure(message): return message
        }
        helper.certSHA256 = pairing.certSHA256 ?? lan?.pin
        helper.lanPort = pairing.lanPort ?? (lan == nil ? nil : Self.lanDefaultPort)
        do {
            try store.add(helper)
        } catch {
            try? tokens.delete(helper.id)
            return "Couldn't save \(host): \(error.localizedDescription)"
        }
        helpers = store.helpers
        selectedHelperID = helper.id
        return nil
    }

    /// Pairs an existing computer again, replacing its token.
    func pair(_ helper: Helper, code: String) async -> String? {
        let error = await pairAndStore(helper, code: code, lan: nil)
        if error == nil, feedback?.pairAgain?.id == helper.id { feedback = nil }
        return error
    }

    func delete(_ helper: Helper) {
        try? store.delete(helper)
        if let pin = helper.certSHA256 { HelperClient.forgetLANSession(pin: pin) }
        helpers = store.helpers
        statuses[helper.id] = nil
        statusRoutes[helper.id] = nil
        if selectedHelperID == helper.id { selectedHelperID = helpers.first?.id }
    }

    /// Port the LAN listener uses until /pair says otherwise.
    static let lanDefaultPort = 47812

    /// Pairs an existing computer and stores its new token and fingerprint.
    private func pairAndStore(_ helper: Helper, code: String, lan: (address: String, pin: String)?) async -> String? {
        switch await pairForToken(helper, code: code, lan: lan) {
        case let .failure(message): return message
        case let .success(pairing):
            if let fp = pairing.certSHA256 ?? lan?.pin {
                learn(fp, lanPort: pairing.lanPort ?? (lan == nil ? nil : Self.lanDefaultPort), for: helper.id)
            }
            return nil
        }
    }

    private enum PairOutcome { case success(Pairing), failure(String) }

    /// Runs /pair (over pinned TLS to the LAN address first, if given) and
    /// saves the token. The code is single use, so the tailnet is tried only
    /// when the LAN connection was refused outright.
    private func pairForToken(_ helper: Helper, code: String, lan: (address: String, pin: String)?) async -> PairOutcome {
        let code = code.filter(\.isNumber)
        guard code.count == 6 else { return .failure("Enter the 6-digit code from phrased pair.") }
        do {
            let pairing: Pairing
            if let lan {
                let route = HelperClient.Route.lan(host: lan.address, port: Self.lanDefaultPort, pin: lan.pin)
                do {
                    pairing = try await makeClient(helper, nil, route).pair(code: code, device: device)
                } catch HelperClient.Failure.unreachable {
                    pairing = try await makeClient(helper, nil, .tailnet).pair(code: code, device: device)
                }
            } else {
                pairing = try await makeClient(helper, nil, .tailnet).pair(code: code, device: device)
            }
            try tokens.set(pairing.token, for: helper.id)
            return .success(pairing)
        } catch HelperClient.Failure.noPairingWindow {
            return .failure("Run phrased pair on \(helper.name) first.")
        } catch HelperClient.Failure.unauthorized {
            return .failure("That code didn't match. Check it, or run phrased pair again.")
        } catch HelperClient.Failure.rateLimited {
            return .failure("Too many pairing attempts. Run phrased pair on \(helper.name) again.")
        } catch let failure as HelperClient.Failure {
            return .failure(Self.feedback(for: failure, on: helper).message ?? "Couldn't pair with \(helper.name).")
        } catch {
            return .failure("Couldn't pair with \(helper.name): \(error.localizedDescription)")
        }
    }

    private func client(for helper: Helper, _ route: HelperClient.Route) -> any HelperAPI {
        makeClient(helper, (try? tokens.get(helper.id)) ?? nil, route)
    }
}
