import Foundation
import Observation

/// A computer running `phrased`, added by tailnet name. Local to each
/// device; its token lives in the Keychain under `id`.
struct Helper: Codable, Hashable, Identifiable, Sendable {
    static let defaultPort = 47811

    var id = UUID()
    var name: String
    var host: String
    var port: Int = Helper.defaultPort
    /// The LAN certificate's fingerprint and port (#11), taken only from
    /// /pair, the QR code or an authenticated /info; never from Bonjour.
    var certSHA256: String?
    var lanPort: Int?
    /// The LAN address from a QR code made with Tailscale off; such a
    /// computer has an empty `host` and is reached only this way.
    var lanHost: String?

    /// The machine name from a MagicDNS name ("lib-2833" for
    /// lib-2833.tail1fdac.ts.net); other hosts as they are.
    /// False for a computer paired with Tailscale off (#11).
    var hasTailnetHost: Bool { !host.isEmpty }

    static func displayName(for host: String) -> String {
        host.hasSuffix(".ts.net") ? String(host.prefix { $0 != "." }) : host
    }

    /// Whether two hosts name the same computer: equal (ignoring case and a
    /// trailing dot), or a bare short name and a MagicDNS name whose first
    /// label is that name. Two different FQDNs never match on their first label.
    static func sameMachine(_ a: String, _ b: String) -> Bool {
        func clean(_ h: String) -> String { h.lowercased().hasSuffix(".") ? String(h.lowercased().dropLast()) : h.lowercased() }
        let (a, b) = (clean(a), clean(b))
        if a == b { return true }
        func short(_ bare: String, matches fqdn: String) -> Bool {
            !bare.contains(".") && !bare.contains(":") && fqdn.hasSuffix(".ts.net")
                && fqdn.prefix { $0 != "." } == bare
        }
        return short(a, matches: b) || short(b, matches: a)
    }
}

/// Where pairing tokens are kept: the Keychain in the app (`TokenStore`), memory in tests.
protocol TokenVault: Sendable {
    func set(_ token: String, for helperID: UUID) throws
    func get(_ helperID: UUID) throws -> String?
    func delete(_ helperID: UUID) throws
}

extension TokenStore: TokenVault {}

/// Tokens in memory, for unit and UI tests: nothing outlives the run.
final class MemoryTokens: TokenVault, @unchecked Sendable {
    private let lock = NSLock()
    private var tokens: [UUID: String] = [:]
    /// Makes `delete` throw, to test that a failure is shown.
    var failDelete: (any Error)?

    func set(_ token: String, for helperID: UUID) throws { lock.withLock { tokens[helperID] = token } }
    func get(_ helperID: UUID) throws -> String? { lock.withLock { tokens[helperID] } }
    func delete(_ helperID: UUID) throws {
        if let failDelete { throw failDelete }
        lock.withLock { tokens[helperID] = nil }
    }
}

/// The computers on this device and everything about reaching them: the records (in
/// UserDefaults, so they can never sync by accident), their tokens, their LAN pins, which one
/// is selected here, what Bonjour sees nearby, and the route to each (#43).
@MainActor
@Observable
final class Computers {
    /// Builds the connection for a computer, token and route; `HelperClient` in the app.
    typealias Connect = @MainActor (Helper, String?, HelperClient.Route) -> any HelperAPI

    private static let helpersKey = "helpers"
    private static let selectedKey = "selectedHelperID"

    @ObservationIgnored private let defaults: UserDefaults
    @ObservationIgnored private let tokens: any TokenVault
    @ObservationIgnored private let connect: Connect

    private(set) var all: [Helper]
    /// Phrasedeck helpers Bonjour sees on the home network.
    private(set) var nearby: [NearbyService] = []

    var selectedID: UUID? {
        didSet { defaults.set(selectedID?.uuidString, forKey: Self.selectedKey) }
    }

    var selected: Helper? { all.first { $0.id == selectedID } }

    init(defaults: UserDefaults = .standard, tokens: any TokenVault = TokenStore(),
         connect: @escaping Connect = { HelperClient(helper: $0, token: $1, route: $2) }) {
        self.defaults = defaults
        self.tokens = tokens
        self.connect = connect
        all = defaults.data(forKey: Self.helpersKey)
            .flatMap { try? JSONDecoder().decode([Helper].self, from: $0) } ?? []
        let stored = defaults.string(forKey: Self.selectedKey).flatMap(UUID.init(uuidString:))
        selectedID = all.contains { $0.id == stored } ? stored : nil
    }

    // MARK: routes and connections

    func setNearby(_ services: [NearbyService]) { nearby = services }

    /// The LAN, pinned, when the computer is nearby and has a fingerprint from a trusted
    /// channel; otherwise the tailnet. The one place a route is chosen.
    func route(for helper: Helper) -> HelperClient.Route {
        guard let pin = helper.certSHA256 else { return .tailnet }
        if let address = nearby.first(where: { $0.matches(helper) && $0.address != nil })?.address {
            return .lan(host: address.host, port: address.port, pin: pin)
        }
        // Paired with Tailscale off and not seen by Bonjour: the address from its QR code.
        if !helper.hasTailnetHost, let lanHost = helper.lanHost {
            return .lan(host: lanHost, port: helper.lanPort ?? PairLink.defaultLANPort, pin: pin)
        }
        return .tailnet
    }

    /// A connection with the computer's token, by the chosen route unless one is given.
    func connection(to helper: Helper, via route: HelperClient.Route? = nil) -> any HelperAPI {
        connect(helper, (try? tokens.get(helper.id)) ?? nil, route ?? self.route(for: helper))
    }

    /// A connection without a token, for /pair.
    func pairingConnection(to helper: Helper, via route: HelperClient.Route) -> any HelperAPI {
        connect(helper, nil, route)
    }

    // MARK: records, tokens and pins

    func setToken(_ token: String, for helper: Helper) throws { try tokens.set(token, for: helper.id) }

    /// Adds a newly paired computer; if the record can't be saved, its token is removed too.
    func add(_ helper: Helper) throws {
        do {
            try save(all + [helper])
        } catch {
            try? tokens.delete(helper.id)
            throw error
        }
    }

    /// Stores a fingerprint from /pair, the QR code or /info. A changed one drops the old
    /// pin's connections.
    func learn(_ fingerprint: String, lanPort: Int?, for id: UUID) throws {
        guard var helper = all.first(where: { $0.id == id }) else { return }
        if let old = helper.certSHA256, old != fingerprint { HelperClient.forgetLANSession(pin: old) }
        helper.certSHA256 = fingerprint
        helper.lanPort = lanPort ?? helper.lanPort
        try save(all.map { $0.id == id ? helper : $0 })
    }

    /// Removes the computer: its token first, so a Keychain failure leaves everything as it
    /// was and is thrown, then its record and its pin's connections. If it was selected, the
    /// first remaining computer is.
    func delete(_ helper: Helper) throws {
        try tokens.delete(helper.id)
        try save(all.filter { $0.id != helper.id })
        if let pin = helper.certSHA256 { HelperClient.forgetLANSession(pin: pin) }
        if selectedID == helper.id { selectedID = all.first?.id }
    }

    private func save(_ helpers: [Helper]) throws {
        defaults.set(try JSONEncoder().encode(helpers), forKey: Self.helpersKey)
        all = helpers
    }
}
