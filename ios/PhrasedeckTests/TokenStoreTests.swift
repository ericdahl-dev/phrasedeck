import Foundation
import Testing
@testable import Phrasedeck

// Runs in the app (test host): Keychain calls need an app's entitlements.
// Serialized because every test shares the one Keychain.
@Suite(.serialized)
struct TokenStoreTests {
    let store = TokenStore(service: "dev.ericdahl.phrasedeck.tests")

    @Test func roundTrip() throws {
        let id = UUID()
        defer { try? store.delete(id) }

        try store.set("tok-1", for: id)

        #expect(try store.get(id) == "tok-1")
    }

    @Test func setOverwritesTheOldValue() throws {
        let id = UUID()
        defer { try? store.delete(id) }

        try store.set("old", for: id)
        try store.set("new", for: id)

        #expect(try store.get(id) == "new")
    }

    @Test func deleteRemovesTheItem() throws {
        let id = UUID()
        try store.set("tok", for: id)

        try store.delete(id)

        #expect(try store.get(id) == nil)
    }

    @Test func deletingAMissingItemIsNotAnError() throws {
        try store.delete(UUID())
    }

    @Test func tokensAreScopedPerHelper() throws {
        let a = UUID(), b = UUID()
        defer { try? store.delete(a); try? store.delete(b) }

        try store.set("a", for: a)
        try store.set("b", for: b)

        #expect(try store.get(a) == "a")
        #expect(try store.get(b) == "b")
    }

    @Test func itemsAreThisDeviceOnlyAndNeverSync() throws {
        let id = UUID()
        defer { try? store.delete(id) }
        try store.set("tok", for: id)

        let attributes = try #require(try store.attributes(for: id))
        #expect(attributes[kSecAttrAccessible as String] as? String
                == kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly as String)
        #expect((attributes[kSecAttrSynchronizable as String] as? Bool ?? false) == false)
    }
}
