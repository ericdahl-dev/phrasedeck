import Foundation
import Testing
@testable import Phrasedeck

@Suite(.serialized)
struct HelperStoreTests {
    let defaults: UserDefaults
    let tokens = TokenStore(service: "dev.ericdahl.phrasedeck.tests")

    init() throws {
        let suite = "HelperStoreTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    func makeStore() -> HelperStore { HelperStore(defaults: defaults, tokens: tokens) }

    @Test func helpersSurviveAReload() throws {
        let store = makeStore()
        let mac = Helper(name: "Mac", host: "erics-mac", port: 47811)
        try store.add(mac)

        #expect(makeStore().helpers == [mac])
    }

    @Test func thePortDefaultsTo47811() {
        #expect(Helper(name: "omarchy", host: "omarchy-laptop").port == 47811)
    }

    @Test func theSelectedHelperSurvivesAReload() throws {
        let store = makeStore()
        let mac = Helper(name: "Mac", host: "erics-mac")
        let laptop = Helper(name: "omarchy", host: "omarchy-laptop")
        try store.add(mac)
        try store.add(laptop)

        store.selectedHelperID = laptop.id

        #expect(makeStore().selectedHelperID == laptop.id)
        #expect(makeStore().selectedHelper == laptop)
    }

    @Test func deleteRemovesTheHelperAndClearsItsKeychainItem() throws {
        let store = makeStore()
        let mac = Helper(name: "Mac", host: "erics-mac")
        try store.add(mac)
        store.selectedHelperID = mac.id
        try tokens.set("tok", for: mac.id)

        try store.delete(mac)

        #expect(makeStore().helpers.isEmpty)
        #expect(makeStore().selectedHelperID == nil)
        #expect(try tokens.get(mac.id) == nil)
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

@Suite(.serialized)
struct HelperLANFieldTests {
    @Test func aComputerSavedBeforeLANSupportStillLoads() throws {
        let suite = "HelperLANFieldTests.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let id = UUID()
        defaults.set(Data(#"[{"id":"\#(id.uuidString)","name":"lib-2833","host":"lib-2833","port":47811}]"#.utf8),
                     forKey: "helpers")

        let helpers = HelperStore(defaults: defaults, tokens: TokenStore(service: "dev.ericdahl.phrasedeck.tests")).helpers

        #expect(helpers == [Helper(id: id, name: "lib-2833", host: "lib-2833", port: 47811)])
        #expect(helpers.first?.certSHA256 == nil && helpers.first?.lanPort == nil)
    }

    @Test func updateReplacesTheComputerInPlace() throws {
        let suite = "HelperLANFieldTests.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = HelperStore(defaults: defaults, tokens: TokenStore(service: "dev.ericdahl.phrasedeck.tests"))
        var a = Helper(name: "a", host: "a.tail1.ts.net")
        let b = Helper(name: "b", host: "b.tail1.ts.net")
        try store.add(a)
        try store.add(b)

        a.certSHA256 = String(repeating: "ab", count: 32)
        a.lanPort = 47812
        try store.update(a)

        #expect(store.helpers == [a, b])
    }
}
