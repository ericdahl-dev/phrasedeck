import Foundation
import SwiftData
import Testing
@testable import Phrasedeck

@MainActor
struct DeckEditTests {
    let container: ModelContainer
    var context: ModelContext { container.mainContext }

    init() throws {
        container = try ModelContainer(
            for: Page.self, Phrase.self,
            configurations: ModelConfiguration(isStoredInMemoryOnly: true, cloudKitDatabase: .none))
    }

    func pages() throws -> [Page] { try context.fetch(FetchDescriptor<Page>(sortBy: [SortDescriptor(\.order)])) }

    func labels(_ page: Page) -> [String] { page.sortedPhrases.map(\.label) }

    @Test func addedPhrasesGoToTheEndOfTheirPage() throws {
        let page = Deck.addPage(named: "Agents", in: context)
        Deck.addPhrase(PhraseDraft(label: "a", text: "a"), to: page, in: context)
        Deck.addPhrase(PhraseDraft(label: "b", text: "b", pressEnter: true, confirm: true, colorName: "red"),
                       to: page, in: context)

        #expect(labels(page) == ["a", "b"])
        let b = try #require(page.sortedPhrases.last)
        #expect(b.pressEnter && b.confirm && b.colorName == "red")
    }

    @Test func pagesAreAddedInOrderAndReordered() throws {
        for name in ["Agents", "Merges", "ND"] { Deck.addPage(named: name, in: context) }
        #expect(try pages().map(\.name) == ["Agents", "Merges", "ND"])

        Deck.movePages(try pages(), from: IndexSet(integer: 2), to: 0)
        #expect(try pages().map(\.name) == ["ND", "Agents", "Merges"])
        #expect(try pages().map(\.order) == [0, 1, 2])
    }

    @Test func renamingAPageKeepsItsPhrases() throws {
        let page = Deck.addPage(named: "Merges", in: context)
        Deck.addPhrase(PhraseDraft(label: "m", text: "merge "), to: page, in: context)
        Deck.rename(page, to: "  Merge  ")
        #expect(page.name == "Merge")
        #expect(labels(page) == ["m"])
    }

    @Test func movingAPhraseToAnotherPageAppendsItAndClosesTheGap() throws {
        let agents = Deck.addPage(named: "Agents", in: context)
        let merges = Deck.addPage(named: "Merges", in: context)
        for l in ["a", "b", "c"] { Deck.addPhrase(PhraseDraft(label: l, text: l), to: agents, in: context) }
        Deck.addPhrase(PhraseDraft(label: "m", text: "m"), to: merges, in: context)

        let b = try #require(agents.sortedPhrases.first { $0.label == "b" })
        var draft = PhraseDraft(b)
        draft.page = merges
        Deck.save(draft, into: b)

        #expect(labels(agents) == ["a", "c"])
        #expect(agents.sortedPhrases.map(\.order) == [0, 1])
        #expect(labels(merges) == ["m", "b"])
    }

    @Test func editingWithoutMovingKeepsThePlace() throws {
        let page = Deck.addPage(named: "Agents", in: context)
        for l in ["a", "b", "c"] { Deck.addPhrase(PhraseDraft(label: l, text: l), to: page, in: context) }
        let b = try #require(page.sortedPhrases[1] as Phrase?)
        var draft = PhraseDraft(b)
        draft.label = "B"
        draft.text = "line one\nline two"
        Deck.save(draft, into: b)

        #expect(labels(page) == ["a", "B", "c"])
        #expect(b.text == "line one\nline two")
    }

    @Test func deletingAPhraseClosesTheGap() throws {
        let page = Deck.addPage(named: "Agents", in: context)
        for l in ["a", "b", "c"] { Deck.addPhrase(PhraseDraft(label: l, text: l), to: page, in: context) }
        Deck.delete(try #require(page.sortedPhrases.first), in: context)
        try context.save()

        #expect(labels(page) == ["b", "c"])
        #expect(page.sortedPhrases.map(\.order) == [0, 1])
    }

    @Test func deletingAPageDeletesItsPhrasesAndRenumbersPages() throws {
        let agents = Deck.addPage(named: "Agents", in: context)
        let merges = Deck.addPage(named: "Merges", in: context)
        Deck.addPage(named: "ND", in: context)
        Deck.addPhrase(PhraseDraft(label: "m", text: "m"), to: merges, in: context)
        Deck.addPhrase(PhraseDraft(label: "a", text: "a"), to: agents, in: context)

        Deck.delete(merges, in: context)
        try context.save()

        #expect(try pages().map(\.name) == ["Agents", "ND"])
        #expect(try pages().map(\.order) == [0, 1])
        #expect(try context.fetch(FetchDescriptor<Phrase>()).map(\.label) == ["a"])
    }

    @Test func aBlankDraftCannotBeSaved() {
        #expect(!PhraseDraft(label: " ", text: "x").isValid)
        #expect(!PhraseDraft(label: "x", text: "").isValid)
        #expect(PhraseDraft(label: "x", text: "y").isValid)
    }

    @Test func thereAreSixNamedColors() {
        #expect(PhraseColor.all.count == 6)
        #expect(PhraseColor.all.contains("blue"))
    }
}
