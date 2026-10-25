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

    struct Feedback: Equatable {
        enum Tone: Equatable { case success, amber, red }
        let tone: Tone
        let message: String?
        /// Set after a 401, so the banner can offer "Pair again".
        var pairAgain: Helper?
    }

    typealias MakeClient = @MainActor (Helper, String?) -> any HelperAPI

    private let store: HelperStore
    private let tokens: TokenStore
    private let device: String
    private let makeClient: MakeClient

    private(set) var helpers: [Helper]
    private(set) var statuses: [UUID: Status] = [:]
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
            try await client(for: helper).type(text: text, enter: enter)
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

    func refreshStatus(of helper: Helper) async {
        let status: Status
        do {
            status = try await client(for: helper).health().accessibilityOK ? .reachable : .notReady
        } catch {
            status = .unreachable
        }
        statuses[helper.id] = status
    }

    // MARK: computers

    /// Pairs with a new computer by tailnet name and adds it only once
    /// pairing worked. Returns a message to show, or nil on success.
    func addHelper(host: String, port: Int?, code: String) async -> String? {
        let host = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !host.isEmpty else { return "Enter the computer's tailnet name." }
        let port = port ?? Helper.defaultPort
        // Scanning a computer that's already here pairs it again instead of adding a copy.
        if let existing = helpers.first(where: { Helper.sameMachine($0.host, host) && $0.port == port }) {
            if let error = await pair(existing, code: code) { return error }
            selectedHelperID = existing.id
            return nil
        }
        let helper = Helper(name: Helper.displayName(for: host), host: host, port: port)
        if let error = await pairAndStore(helper, code: code) { return error }
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
        let error = await pairAndStore(helper, code: code)
        if error == nil, feedback?.pairAgain == helper { feedback = nil }
        return error
    }

    func delete(_ helper: Helper) {
        try? store.delete(helper)
        helpers = store.helpers
        statuses[helper.id] = nil
        if selectedHelperID == helper.id { selectedHelperID = helpers.first?.id }
    }

    private func pairAndStore(_ helper: Helper, code: String) async -> String? {
        let code = code.filter(\.isNumber)
        guard code.count == 6 else { return "Enter the 6-digit code from phrased pair." }
        do {
            let token = try await makeClient(helper, nil).pair(code: code, device: device).token
            try tokens.set(token, for: helper.id)
            return nil
        } catch HelperClient.Failure.noPairingWindow {
            return "Run phrased pair on \(helper.name) first."
        } catch HelperClient.Failure.unauthorized {
            return "That code didn't match. Check it, or run phrased pair again."
        } catch HelperClient.Failure.rateLimited {
            return "Too many pairing attempts. Run phrased pair on \(helper.name) again."
        } catch let failure as HelperClient.Failure {
            return Self.feedback(for: failure, on: helper).message
        } catch {
            return "Couldn't pair with \(helper.name): \(error.localizedDescription)"
        }
    }

    private func client(for helper: Helper) -> any HelperAPI {
        makeClient(helper, (try? tokens.get(helper.id)) ?? nil)
    }
}
