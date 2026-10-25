import CoreData
import Foundation
import SwiftData
import Testing
@testable import Phrasedeck

/// With sync on, each device seeds its own starter deck before the other's arrives. These
/// tests copy one store's rows into another, as a CloudKit import would, and merge.
@MainActor
struct SeedMergeTests {
    static func emptyStore() throws -> ModelContainer {
        try ModelContainer(
            for: Page.self, Phrase.self,
            configurations: ModelConfiguration(isStoredInMemoryOnly: true, cloudKitDatabase: .none))
    }

    static func seededStore() throws -> ModelContainer {
        let container = try emptyStore()
        let suite = "SeedMergeTests.\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
        try Seed.insertMissing(container.mainContext, defaults: defaults)
        return container
    }

    /// Copies every page and phrase, uid included, as an import from another device would.
    static func importRows(from source: ModelContext, into target: ModelContext) throws {
        for page in try source.fetch(FetchDescriptor<Page>()) {
            let copy = Page(name: page.name, order: page.order, seedKey: page.seedKey)
            copy.uid = page.uid
            target.insert(copy)
            for p in page.sortedPhrases {
                let phrase = Phrase(label: p.label, text: p.text, pressEnter: p.pressEnter, confirm: p.confirm,
                                    colorName: p.colorName, order: p.order, page: copy, seedKey: p.seedKey)
                phrase.uid = p.uid
                target.insert(phrase)
            }
        }
        try target.save()
    }

    func survivors(_ context: ModelContext) throws -> [String] {
        try context.fetch(FetchDescriptor<Phrase>()).map(\.uid).sorted()
    }

    @Test func twoSeededStoresMergeIntoOneRowPerKey() throws {
        let a = try Self.seededStore(), b = try Self.seededStore()
        try Self.importRows(from: b.mainContext, into: a.mainContext)
        #expect(try a.mainContext.fetchCount(FetchDescriptor<Phrase>()) == 10)

        try Seed.mergeDuplicates(a.mainContext)

        let pages = try a.mainContext.fetch(FetchDescriptor<Page>())
        #expect(pages.count == 1)
        let page = try #require(pages.first)
        #expect(page.sortedPhrases.map(\.seedKey) == Seed.rows.map(\.key))
        #expect(page.sortedPhrases.map(\.order) == [0, 1, 2, 3, 4])
    }

    @Test func bothDevicesKeepTheSameRows() throws {
        // If each device kept its own copy and deleted the other's, sync would delete both.
        // Each device sees the same rows, its own first.
        let a = try Self.seededStore(), b = try Self.seededStore()
        let onA = try Self.emptyStore(), onB = try Self.emptyStore()
        try Self.importRows(from: a.mainContext, into: onA.mainContext)
        try Self.importRows(from: b.mainContext, into: onA.mainContext)
        try Self.importRows(from: b.mainContext, into: onB.mainContext)
        try Self.importRows(from: a.mainContext, into: onB.mainContext)

        try Seed.mergeDuplicates(onA.mainContext)
        try Seed.mergeDuplicates(onB.mainContext)

        #expect(try survivors(onA.mainContext).count == 5)
        #expect(try survivors(onA.mainContext) == survivors(onB.mainContext))
    }

    @Test func ericsOwnPhrasesOnADuplicatePageMoveToTheKeptPage() throws {
        let a = try Self.seededStore(), b = try Self.seededStore()
        let bPage = try #require(try b.mainContext.fetch(FetchDescriptor<Page>()).first)
        Deck.addPhrase(PhraseDraft(label: "Mine", text: "mine"), to: bPage, in: b.mainContext)
        try b.mainContext.save()
        try Self.importRows(from: b.mainContext, into: a.mainContext)

        try Seed.mergeDuplicates(a.mainContext)

        let page = try #require(try a.mainContext.fetch(FetchDescriptor<Page>()).first)
        #expect(page.sortedPhrases.count == 6)
        #expect(page.sortedPhrases.map(\.order) == [0, 1, 2, 3, 4, 5])
        #expect(page.sortedPhrases.contains { $0.label == "Mine" })
    }

    @Test func rowsWithoutAUIDGetOneBeforeMerging() throws {
        let a = try Self.seededStore()
        for p in try a.mainContext.fetch(FetchDescriptor<Phrase>()) { p.uid = "" }

        try Seed.mergeDuplicates(a.mainContext)

        #expect(try a.mainContext.fetch(FetchDescriptor<Phrase>()).allSatisfy { !$0.uid.isEmpty })
        #expect(try a.mainContext.fetchCount(FetchDescriptor<Phrase>()) == 5)
    }

    /// After a CloudKit import the merge runs on its own, not from a view.
    @Test func aRemoteChangeMergesDuplicates() async throws {
        let mine = try Self.seededStore()
        let theirs = try Self.seededStore()
        let pageCount = try mine.mainContext.fetchCount(FetchDescriptor<Page>())
        let merge = SyncMerge(container: mine)
        try Self.importRows(from: theirs.mainContext, into: mine.mainContext)

        NotificationCenter.default.post(name: .NSPersistentStoreRemoteChange, object: nil)
        await Task.yield()

        #expect(try mine.mainContext.fetchCount(FetchDescriptor<Page>()) == pageCount)
        withExtendedLifetime(merge) {}
    }
}
