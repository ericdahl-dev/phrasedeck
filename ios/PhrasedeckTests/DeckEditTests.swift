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
        let page = try Deck.addPage(named: "Agents", in: context)
        try Deck.addPhrase(PhraseDraft(label: "a", text: "a"), to: page, in: context)
        try Deck.addPhrase(PhraseDraft(label: "b", text: "b", pressEnter: true, confirm: true, colorName: "red"),
                       to: page, in: context)

        #expect(labels(page) == ["a", "b"])
        let b = try #require(page.sortedPhrases.last)
        #expect(b.pressEnter && b.confirm && b.colorName == "red")
    }

    @Test func pagesAreAddedInOrderAndReordered() throws {
        for name in ["Agents", "Merges", "ND"] { try Deck.addPage(named: name, in: context) }
        #expect(try pages().map(\.name) == ["Agents", "Merges", "ND"])

        try Deck.movePages(try pages(), from: IndexSet(integer: 2), to: 0)
        #expect(try pages().map(\.name) == ["ND", "Agents", "Merges"])
        #expect(try pages().map(\.order) == [0, 1, 2])
    }

    @Test func renamingAPageKeepsItsPhrases() throws {
        let page = try Deck.addPage(named: "Merges", in: context)
        try Deck.addPhrase(PhraseDraft(label: "m", text: "merge "), to: page, in: context)
        try Deck.rename(page, to: "  Merge  ")
        #expect(page.name == "Merge")
        #expect(labels(page) == ["m"])
    }

    @Test func movingAPhraseToAnotherPageAppendsItAndClosesTheGap() throws {
        let agents = try Deck.addPage(named: "Agents", in: context)
        let merges = try Deck.addPage(named: "Merges", in: context)
        for l in ["a", "b", "c"] { try Deck.addPhrase(PhraseDraft(label: l, text: l), to: agents, in: context) }
        try Deck.addPhrase(PhraseDraft(label: "m", text: "m"), to: merges, in: context)

        let b = try #require(agents.sortedPhrases.first { $0.label == "b" })
        var draft = PhraseDraft(b)
        draft.page = merges
        try Deck.save(draft, into: b)

        #expect(labels(agents) == ["a", "c"])
        #expect(agents.sortedPhrases.map(\.order) == [0, 1])
        #expect(labels(merges) == ["m", "b"])
    }

    @Test func editingWithoutMovingKeepsThePlace() throws {
        let page = try Deck.addPage(named: "Agents", in: context)
        for l in ["a", "b", "c"] { try Deck.addPhrase(PhraseDraft(label: l, text: l), to: page, in: context) }
        let b = try #require(page.sortedPhrases[1] as Phrase?)
        var draft = PhraseDraft(b)
        draft.label = "B"
        draft.text = "line one\nline two"
        try Deck.save(draft, into: b)

        #expect(labels(page) == ["a", "B", "c"])
        #expect(b.text == "line one\nline two")
    }

    @Test func deletingAPhraseClosesTheGap() throws {
        let page = try Deck.addPage(named: "Agents", in: context)
        for l in ["a", "b", "c"] { try Deck.addPhrase(PhraseDraft(label: l, text: l), to: page, in: context) }
        try Deck.delete(try #require(page.sortedPhrases.first), in: context)

        #expect(labels(page) == ["b", "c"])
        #expect(page.sortedPhrases.map(\.order) == [0, 1])
    }

    @Test func deletingAPageDeletesItsPhrasesAndRenumbersPages() throws {
        let agents = try Deck.addPage(named: "Agents", in: context)
        let merges = try Deck.addPage(named: "Merges", in: context)
        try Deck.addPage(named: "ND", in: context)
        try Deck.addPhrase(PhraseDraft(label: "m", text: "m"), to: merges, in: context)
        try Deck.addPhrase(PhraseDraft(label: "a", text: "a"), to: agents, in: context)

        try Deck.delete(merges, in: context)

        #expect(try pages().map(\.name) == ["Agents", "ND"])
        #expect(try pages().map(\.order) == [0, 1])
        #expect(try context.fetch(FetchDescriptor<Phrase>()).map(\.label) == ["a"])
    }

    @Test func theSpanIsSavedAndNewPhrasesAreOneByOne() throws {
        let page = try Deck.addPage(named: "Agents", in: context)
        let phrase = try Deck.addPhrase(PhraseDraft(label: "a", text: "a"), to: page, in: context)
        #expect(phrase.spanColumns == 1 && phrase.spanRows == 1)

        var draft = PhraseDraft(phrase)
        draft.spanColumns = 2
        draft.spanRows = 3
        try Deck.save(draft, into: phrase)
        #expect(phrase.spanColumns == 2 && phrase.spanRows == 3)
        #expect(PhraseDraft(phrase).spanColumns == 2)

        let big = try Deck.addPhrase(PhraseDraft(label: "b", text: "b", spanColumns: 3, spanRows: 1), to: page, in: context)
        #expect(big.spanColumns == 3 && big.spanRows == 1)
    }

    /// Every edit is saved by Deck itself, so no view has to remember to.
    @Test func everyEditIsSaved() throws {
        let agents = try Deck.addPage(named: "Agents", in: context)
        #expect(!context.hasChanges)
        let merges = try Deck.addPage(named: "Merges", in: context)
        let phrase = try Deck.addPhrase(PhraseDraft(label: "a", text: "a"), to: agents, in: context)
        #expect(!context.hasChanges)

        try Deck.rename(agents, to: "Agent")
        #expect(!context.hasChanges)
        try Deck.movePages(try pages(), from: IndexSet(integer: 1), to: 0)
        #expect(!context.hasChanges)
        var draft = PhraseDraft(phrase)
        draft.label = "A"
        draft.page = merges
        try Deck.save(draft, into: phrase)
        #expect(!context.hasChanges)
        try Deck.delete(phrase, in: context)
        #expect(!context.hasChanges)
        try Deck.delete(merges, in: context)
        #expect(!context.hasChanges)
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
