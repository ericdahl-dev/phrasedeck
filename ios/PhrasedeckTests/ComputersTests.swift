import Foundation
import Testing
@testable import Phrasedeck

@MainActor
struct ComputersTests {
    let defaults: UserDefaults
    let tokens = MemoryTokens()

    init() throws {
        let suite = "ComputersTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    func makeComputers() -> Computers {
        Computers(defaults: defaults, tokens: tokens) { _, _, _ in FakeHelperAPI() }
    }

    @Test func computersSurviveAReload() throws {
        let mac = Helper(name: "Mac", host: "erics-mac", port: 47811)
        try makeComputers().add(mac)

        #expect(makeComputers().all == [mac])
    }

    @Test func thePortDefaultsTo47811() {
        #expect(Helper(name: "omarchy", host: "omarchy-laptop").port == 47811)
    }

    @Test func theSelectionSurvivesAReload() throws {
        let computers = makeComputers()
        let mac = Helper(name: "Mac", host: "erics-mac")
        let laptop = Helper(name: "omarchy", host: "omarchy-laptop")
        try computers.add(mac)
        try computers.add(laptop)

        computers.selectedID = laptop.id

        #expect(makeComputers().selectedID == laptop.id)
        #expect(makeComputers().selected == laptop)
    }

    @Test func aStoredSelectionOfAMissingComputerIsDropped() {
        defaults.set(UUID().uuidString, forKey: "selectedHelperID")
        #expect(makeComputers().selectedID == nil)
    }

    @Test func deleteRemovesTheComputerAndItsToken() throws {
        let computers = makeComputers()
        let mac = Helper(name: "Mac", host: "erics-mac")
        try computers.add(mac)
        computers.selectedID = mac.id
        try computers.setToken("tok", for: mac)

        try computers.delete(mac)

        #expect(makeComputers().all.isEmpty)
        #expect(makeComputers().selectedID == nil)
        #expect(try tokens.get(mac.id) == nil)
    }

    @Test func learningAFingerprintIsStored() throws {
        let computers = makeComputers()
        let mac = Helper(name: "Mac", host: "erics-mac")
        try computers.add(mac)

        try computers.learn(String(repeating: "ab", count: 32), lanPort: 47900, for: mac.id)

        let stored = try #require(makeComputers().all.first)
        #expect(stored.certSHA256 == String(repeating: "ab", count: 32))
        #expect(stored.lanPort == 47900)
    }

    @Test func aConnectionCarriesTheTokenAndTheChosenRoute() throws {
        var seen: [(String?, HelperClient.Route)] = []
        let computers = Computers(defaults: defaults, tokens: tokens) { _, token, route in
            seen.append((token, route))
            return FakeHelperAPI()
        }
        var mac = Helper(name: "Mac", host: "erics-mac")
        mac.certSHA256 = String(repeating: "ab", count: 32)
        try computers.add(mac)
        try computers.setToken("tok", for: mac)
        computers.setNearby([NearbyService(tailnetName: nil, address: LANAddress(host: "192.168.1.9", port: 47812),
                                           advertisedFingerprint: mac.certSHA256)])

        _ = computers.connection(to: mac)
        _ = computers.connection(to: mac, via: .tailnet)
        _ = computers.pairingConnection(to: mac, via: .tailnet)

        #expect(seen.map(\.0) == ["tok", "tok", nil])
        #expect(seen.map(\.1) == [.lan(host: "192.168.1.9", port: 47812, pin: mac.certSHA256!), .tailnet, .tailnet])
    }
}

struct SameMachineTests {
    @Test(arguments: [
        ("lib-2833", "lib-2833.tail1fdac.ts.net", true),
        ("LIB-2833", "lib-2833.Tail1fdac.ts.net.", true),
        ("lib-2833.tail1fdac.ts.net", "lib-2833", true),
        ("lib-2833.tail1fdac.ts.net", "LIB-2833.tail1fdac.ts.net", true),
        ("lib-2833", "lib-2833", true),
        ("100.69.235.48", "100.69.235.48", true),
        // Two different FQDNs never match on their first label alone.
        ("lib-2833.tail1fdac.ts.net", "lib-2833.tailother.ts.net", false),
        ("lib-2833.example.com", "lib-2833.tail1fdac.ts.net", false),
        // A bare name matches only a MagicDNS name, not any FQDN.
        ("lib-2833", "lib-2833.example.com", false),
        ("lib", "lib-2833.tail1fdac.ts.net", false),
        ("100", "100.69.235.48", false),
        ("omarchy-laptop", "lib-2833.tail1fdac.ts.net", false),
    ])
    func matching(a: String, b: String, same: Bool) {
        #expect(Helper.sameMachine(a, b) == same)
    }
}

@MainActor
struct HelperLANFieldTests {
    @Test func aComputerSavedBeforeLANSupportStillLoads() throws {
        let suite = "HelperLANFieldTests.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let id = UUID()
        defaults.set(Data(#"[{"id":"\#(id.uuidString)","name":"lib-2833","host":"lib-2833","port":47811}]"#.utf8),
                     forKey: "helpers")

        let helpers = Computers(defaults: defaults, tokens: MemoryTokens()).all

        #expect(helpers == [Helper(id: id, name: "lib-2833", host: "lib-2833", port: 47811)])
        #expect(helpers.first?.certSHA256 == nil && helpers.first?.lanPort == nil)
    }

    @Test func learningAFingerprintReplacesTheComputerInPlace() throws {
        let suite = "HelperLANFieldTests.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let computers = Computers(defaults: defaults, tokens: MemoryTokens())
        var a = Helper(name: "a", host: "a.tail1.ts.net")
        let b = Helper(name: "b", host: "b.tail1.ts.net")
        try computers.add(a)
        try computers.add(b)

        a.certSHA256 = String(repeating: "ab", count: 32)
        a.lanPort = 47812
        try computers.learn(a.certSHA256!, lanPort: 47812, for: a.id)

        #expect(computers.all == [a, b])
    }
}
