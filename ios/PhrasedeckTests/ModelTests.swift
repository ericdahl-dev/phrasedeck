import Foundation
import SwiftData
import Testing
@testable import Phrasedeck

@MainActor
struct ModelTests {
    let container: ModelContainer
    let defaults: UserDefaults

    init() throws {
        container = try ModelContainer(
            for: Page.self, Phrase.self,
            configurations: ModelConfiguration(isStoredInMemoryOnly: true, cloudKitDatabase: .none))
        let suite = "ModelTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    var context: ModelContext { container.mainContext }

    func phrases() throws -> [Phrase] { try context.fetch(FetchDescriptor<Phrase>()) }

    @Test func seedsTheAgentsPageAndFiveStarterPhrases() throws {
        try Seed.insertMissing(context, defaults: defaults)

        let pages = try context.fetch(FetchDescriptor<Page>())
        #expect(pages.map(\.seedKey) == ["page.agents"])
        #expect(pages.first?.name == "Agents")
        #expect(try phrases().count == 5)
        #expect(try phrases().allSatisfy { $0.page?.seedKey == "page.agents" })
    }

    @Test func seedingTwiceAddsNothingTheSecondTime() throws {
        try Seed.insertMissing(context, defaults: defaults)
        try Seed.insertMissing(context, defaults: defaults)

        #expect(try phrases().count == 5)
        #expect(try context.fetch(FetchDescriptor<Page>()).count == 1)
    }

    @Test func onlyMissingSeedKeysAreInserted() throws {
        // A row already present (say, from another device) is not doubled,
        // even before this device's seed flag is set.
        let page = Page(name: "Agents", order: 0, seedKey: "page.agents")
        context.insert(page)
        context.insert(Phrase(label: "Yes", text: "yes", pressEnter: true, confirm: true, order: 3,
                              page: page, seedKey: "phrase.yes"))

        try Seed.insertMissing(context, defaults: defaults)

        #expect(try phrases().filter { $0.seedKey == "phrase.yes" }.count == 1)
        #expect(try phrases().count == 5)
        #expect(try context.fetch(FetchDescriptor<Page>()).count == 1)
    }

    @Test func aDeletedStarterPhraseIsNotReAddedOnceSeeded() throws {
        try Seed.insertMissing(context, defaults: defaults)
        let yes = try #require(try phrases().first { $0.seedKey == "phrase.yes" })
        context.delete(yes)
        try context.save()

        try Seed.insertMissing(context, defaults: defaults)

        #expect(try phrases().first { $0.seedKey == "phrase.yes" } == nil)
    }

    @Test func starterPhrasesMatchTheDesignTable() throws {
        try Seed.insertMissing(context, defaults: defaults)
        let page = try #require(try context.fetch(FetchDescriptor<Page>()).first)

        let rows = page.sortedPhrases.map { [$0.label, $0.text, "\($0.pressEnter)", "\($0.confirm)"] }
        #expect(rows == [
            ["Agent status", "what is the status of the agents? ask them if needed", "true", "false"],
            ["Status table", "what is the status of the agents? ask them if needed.  show me in a table", "true", "false"],
            ["Merge…", "merge ", "false", "false"],
            ["Yes", "yes", "true", "true"],
            ["Merge it", "Merge it", "true", "true"],
        ])
    }

    @Test func phrasesSortByOrder() {
        let page = Page(name: "P", order: 0)
        context.insert(page)
        for (label, order) in [("c", 2), ("a", 0), ("b", 1)] {
            context.insert(Phrase(label: label, text: label, order: order, page: page))
        }

        #expect(page.sortedPhrases.map(\.label) == ["a", "b", "c"])
    }
}
