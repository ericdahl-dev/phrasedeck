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
