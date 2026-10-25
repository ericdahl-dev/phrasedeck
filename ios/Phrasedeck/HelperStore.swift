import Foundation

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

    /// The machine name from a MagicDNS name ("lib-2833" for
    /// lib-2833.tail1fdac.ts.net); other hosts as they are.
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

/// The computers on this device, in UserDefaults so they can never sync
/// by accident, plus which one is selected here.
final class HelperStore {
    private static let helpersKey = "helpers"
    private static let selectedKey = "selectedHelperID"

    private let defaults: UserDefaults
    private let tokens: TokenStore

    init(defaults: UserDefaults = .standard, tokens: TokenStore = TokenStore()) {
        self.defaults = defaults
        self.tokens = tokens
    }

    var helpers: [Helper] {
        guard let data = defaults.data(forKey: Self.helpersKey) else { return [] }
        return (try? JSONDecoder().decode([Helper].self, from: data)) ?? []
    }

    var selectedHelperID: UUID? {
        get { defaults.string(forKey: Self.selectedKey).flatMap(UUID.init(uuidString:)) }
        set { defaults.set(newValue?.uuidString, forKey: Self.selectedKey) }
    }

    var selectedHelper: Helper? { helpers.first { $0.id == selectedHelperID } }

    func add(_ helper: Helper) throws {
        try save(helpers + [helper])
    }

    /// Replaces the stored computer with the same id.
    func update(_ helper: Helper) throws {
        try save(helpers.map { $0.id == helper.id ? helper : $0 })
    }

    /// Removes the computer and clears its Keychain token.
    func delete(_ helper: Helper) throws {
        try tokens.delete(helper.id)
        try save(helpers.filter { $0.id != helper.id })
        if selectedHelperID == helper.id { selectedHelperID = nil }
    }

    private func save(_ helpers: [Helper]) throws {
        defaults.set(try JSONEncoder().encode(helpers), forKey: Self.helpersKey)
    }
}
