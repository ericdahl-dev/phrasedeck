import Foundation
import SwiftData
import SwiftUI

/// The six colors a phrase button can have. `Color(named:)` draws them.
enum PhraseColor {
    static let all = ["blue", "green", "purple", "orange", "red", "gray"]
}

/// The editor's working copy of a phrase, so Cancel leaves the phrase alone.
struct PhraseDraft {
    var label = ""
    var text = ""
    var pressEnter = false
    var confirm = false
    var colorName = "blue"
    var page: Page?
    var sizeColumns = 1
    var sizeRows = 1

    init(label: String = "", text: String = "", pressEnter: Bool = false, confirm: Bool = false,
         colorName: String = "blue", page: Page? = nil, sizeColumns: Int = 1, sizeRows: Int = 1) {
        self.label = label
        self.text = text
        self.pressEnter = pressEnter
        self.confirm = confirm
        self.colorName = colorName
        self.page = page
        self.sizeColumns = sizeColumns
        self.sizeRows = sizeRows
    }

    init(_ phrase: Phrase) {
        self.init(label: phrase.label, text: phrase.text, pressEnter: phrase.pressEnter,
                  confirm: phrase.confirm, colorName: phrase.colorName, page: phrase.page,
                  sizeColumns: phrase.sizeColumns, sizeRows: phrase.sizeRows)
    }

    var isValid: Bool { !label.trimmingCharacters(in: .whitespaces).isEmpty && !text.isEmpty }
}

/// Edits to the deck. Each one saves itself and throws if the save fails, so views only
/// show the error. Orders stay 0, 1, 2… within a page and across pages, so the deck always
/// swipes and sorts the way it looks.
@MainActor
enum Deck {
    @discardableResult
    static func addPage(named name: String, in context: ModelContext) throws -> Page {
        let count = (try? context.fetchCount(FetchDescriptor<Page>())) ?? 0
        let page = Page(name: name.trimmingCharacters(in: .whitespaces), order: count)
        context.insert(page)
        try saveInserted(page, in: context)
        return page
    }

    static func rename(_ page: Page, to name: String) throws {
        // The Pages sheet renames a row as it disappears, which can be just after it was deleted.
        guard !page.isDeleted, page.modelContext != nil else { return }
        let name = name.trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty, name != page.name else { return }
        page.name = name
        try page.modelContext?.save()
    }

    /// Reorders pages the way `List.onMove` reports it.
    static func movePages(_ pages: [Page], from source: IndexSet, to destination: Int) throws {
        var pages = pages
        pages.move(fromOffsets: source, toOffset: destination)
        for (order, page) in pages.enumerated() { page.order = order }
        try pages.first?.modelContext?.save()
    }

    /// Deletes a page and, through the cascade rule, its phrases.
    static func delete(_ page: Page, in context: ModelContext) throws {
        let rest = ((try? context.fetch(FetchDescriptor<Page>(sortBy: [SortDescriptor(\.order)]))) ?? [])
            .filter { $0 !== page }
        deleteSpots(of: page.sortedPhrases, in: context)
        context.delete(page)
        for (order, page) in rest.enumerated() { page.order = order }
        try context.save()
    }

    @discardableResult
    static func addPhrase(_ draft: PhraseDraft, to page: Page, in context: ModelContext) throws -> Phrase {
        let phrase = Phrase(label: draft.label.trimmingCharacters(in: .whitespaces), text: draft.text,
                            pressEnter: draft.pressEnter, confirm: draft.confirm,
                            colorName: draft.colorName, order: page.sortedPhrases.count, page: page,
                            sizeColumns: draft.sizeColumns, sizeRows: draft.sizeRows)
        context.insert(phrase)
        try saveInserted(phrase, in: context)
        return phrase
    }

    /// Writes the draft into the phrase. A phrase moved to another page goes to its end.
    static func save(_ draft: PhraseDraft, into phrase: Phrase) throws {
        phrase.label = draft.label.trimmingCharacters(in: .whitespaces)
        phrase.text = draft.text
        phrase.pressEnter = draft.pressEnter
        phrase.confirm = draft.confirm
        phrase.colorName = draft.colorName
        phrase.sizeColumns = draft.sizeColumns
        phrase.sizeRows = draft.sizeRows
        if let target = draft.page, target !== phrase.page {
            let old = phrase.page
            phrase.order = target.sortedPhrases.count
            phrase.page = target
            if let old { renumber(old, without: phrase) }
            // Without Spots it shows at the end of the new Page on every Layout width (#81).
            if let context = phrase.modelContext { deleteSpots(of: [phrase], in: context) }
        }
        try phrase.modelContext?.save()
    }

    static func delete(_ phrase: Phrase, in context: ModelContext) throws {
        if let page = phrase.page { renumber(page, without: phrase) }
        deleteSpots(of: [phrase], in: context)
        context.delete(phrase)
        try context.save()
    }

    /// Saves a new row, or takes it back out if the save fails, so retrying doesn't add a second one.
    /// shortcut: not unit-tested, since SwiftData can't be made to fail a save on demand.
    private static func saveInserted(_ model: some PersistentModel, in context: ModelContext) throws {
        do {
            try context.save()
        } catch {
            context.delete(model)
            throw error
        }
    }

    private static func renumber(_ page: Page, without phrase: Phrase) {
        for (order, p) in page.sortedPhrases.filter({ $0 !== phrase }).enumerated() { p.order = order }
    }
}

extension View {
    /// Shows a failed deck save, so an edit never vanishes silently.
    func saveFailureAlert(_ error: Binding<String?>) -> some View {
        alert("Couldn't save", isPresented: Binding(get: { error.wrappedValue != nil },
                                                   set: { if !$0 { error.wrappedValue = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(error.wrappedValue ?? "")
        }
    }
}
