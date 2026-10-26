import Foundation
import SwiftData

/// The kind of screen an arrangement belongs to (#81): the horizontal size class.
enum LayoutWidth: String, CaseIterable, Sendable {
    case compact, regular
}

/// One Button's Spot at one Layout width (#81). It syncs as its own CloudKit record type,
/// CD_SavedSpot, so the Phrase and Page records keep their fields. It finds its Button by
/// `phraseUID` rather than a relationship, for the same reason.
// shortcut: rows of Phrases removed outside Deck (Seed.mergeDuplicates, another phone) stay
// behind; they're never shown and are a few bytes each, so sweep them if that ever matters.
@Model
final class SavedSpot {
    var phraseUID: String = ""
    var layoutWidth: String = ""
    var column: Int = 0
    var row: Int = 0
    /// When this phone or iPad last moved it. Two phones can each add a row for the same Button
    /// before they sync; the newest wins.
    var updated: Date = Date.distantPast

    init(phraseUID: String, width: LayoutWidth, spot: Spot) {
        self.phraseUID = phraseUID
        self.layoutWidth = width.rawValue
        self.column = spot.column
        self.row = spot.row
        self.updated = .now
    }

    /// Whether this row wins over `other` for the same Button: the newer one, and at the same
    /// instant the earlier Spot in reading order, so every phone settles alike.
    func wins(over other: SavedSpot) -> Bool {
        if updated != other.updated { return updated > other.updated }
        return (row, column) < (other.row, other.column)
    }

    var spot: Spot { Spot(column: column, row: row) }
}

extension Deck {
    /// The Spots of the page's Buttons at `width`, by Phrase uid. A Button with no entry has no Spot.
    static func spots(on page: Page, width: LayoutWidth, in context: ModelContext) -> [String: Spot] {
        spots(on: page, width: width, from: saved(width, in: context))
    }

    /// The same from rows already fetched, such as a view's @Query.
    static func spots(on page: Page, width: LayoutWidth, from rows: [SavedSpot]) -> [String: Spot] {
        let uids = Set(page.sortedPhrases.map(\.uid))
        return newest(rows.filter { $0.layoutWidth == width.rawValue }).filter { uids.contains($0.key) }
            .mapValues(\.spot)
    }

    /// Saves Spots at `width`, by Phrase uid. Only Spots that changed are written, and duplicate
    /// rows for a Button are folded into one.
    static func saveSpots(_ spots: [String: Spot], width: LayoutWidth, in context: ModelContext) throws {
        let rows = Dictionary(grouping: saved(width, in: context), by: \.phraseUID)
        for (uid, spot) in spots {
            let existing = (rows[uid] ?? []).sorted { $0.wins(over: $1) }
            for extra in existing.dropFirst() { context.delete(extra) }
            if let row = existing.first {
                guard row.spot != spot else { continue }
                (row.column, row.row, row.updated) = (spot.column, spot.row, .now)
            } else {
                context.insert(SavedSpot(phraseUID: uid, width: width, spot: spot))
            }
        }
        try context.save()
    }

    /// Where the page's Buttons are shown, by Phrase uid: from their Spots at `width`, or packed
    /// from order when the page has none there.
    static func layout(of page: Page, width: LayoutWidth, columns: Int,
                       in context: ModelContext) -> [Packing.Placement<String>] {
        Arrangement.resolve(items(of: page, spots: spots(on: page, width: width, in: context)), columns: columns)
    }

    /// The page's Buttons in order, with their Spots, for `Arrangement.resolve`.
    static func items(of page: Page, spots: [String: Spot]) -> [Arrangement.Item<String>] {
        page.sortedPhrases.map {
            Arrangement.Item(id: $0.uid, columns: $0.sizeColumns, rows: $0.sizeRows, spot: spots[$0.uid])
        }
    }

    /// Drops the Button on `target` (an accessibility move or a drag) and saves the page's Spots at
    /// `width`. Returns how many other Buttons it shifted.
    @discardableResult
    static func move(_ phrase: Phrase, to target: Spot, on page: Page, width: LayoutWidth, columns: Int,
                     in context: ModelContext) throws -> Int {
        let pickup = layout(of: page, width: width, columns: columns, in: context)
        let after = Arrangement.move(phrase.uid, to: target, from: pickup, columns: columns)
        let shifted = pickup.filter { $0.id != phrase.uid && after[$0.id] != Spot(column: $0.x, row: $0.y) }.count
        try saveSpots(after, width: width, in: context)
        return shifted
    }

    /// A VoiceOver move action (spec section 11): one Square that way, as a drop there. Returns
    /// where the Button ended up and how many others shifted, for the announcement.
    static func nudge(_ phrase: Phrase, _ nudge: Arrangement.Nudge, on page: Page, width: LayoutWidth,
                      columns: Int, in context: ModelContext) throws -> (to: Spot, shifted: Int) {
        let shown = layout(of: page, width: width, columns: columns, in: context)
        guard let from = shown.first(where: { $0.id == phrase.uid }) else { return (Spot(column: 0, row: 0), 0) }
        let shifted = try move(phrase, to: nudge.target(from: from), on: page, width: width, columns: columns,
                               in: context)
        let to = layout(of: page, width: width, columns: columns, in: context).first { $0.id == phrase.uid }
        return (to.map { Spot(column: $0.x, row: $0.y) } ?? nudge.target(from: from), shifted)
    }

    static func moveAnnouncement(to spot: Spot, shifted: Int) -> String {
        let others = switch shifted {
        case 0: ""
        case 1: ". 1 Button moved"
        default: ". \(shifted) Buttons moved"
        }
        return "Moved to row \(spot.row + 1), column \(spot.column + 1)\(others)"
    }

    /// Move to Page: the Button goes to the end of `page` (spec rule 8).
    static func moveToPage(_ phrase: Phrase, _ page: Page) throws {
        var draft = PhraseDraft(phrase)
        draft.page = page
        try save(draft, into: phrase)
    }

    /// A drag dropped on another Page (spec section 6): the Button joins `page` at `spot` on this
    /// Layout width, and at the end of it on the others (rule 8).
    static func move(_ phrase: Phrase, to spot: Spot, onto page: Page, width: LayoutWidth, columns: Int,
                     in context: ModelContext) throws {
        try moveToPage(phrase, page)
        try move(phrase, to: spot, on: page, width: width, columns: columns, in: context)
    }

    /// Deletes the Buttons' Spots on every Layout width. The caller saves.
    static func deleteSpots(of phrases: [Phrase], in context: ModelContext) {
        let uids = Set(phrases.map(\.uid))
        for row in (try? context.fetch(FetchDescriptor<SavedSpot>())) ?? [] where uids.contains(row.phraseUID) {
            context.delete(row)
        }
    }

    /// The newest row per Phrase uid.
    private static func newest(_ rows: [SavedSpot]) -> [String: SavedSpot] {
        Dictionary(rows.map { ($0.phraseUID, $0) }, uniquingKeysWith: { $0.wins(over: $1) ? $0 : $1 })
    }

    private static func saved(_ width: LayoutWidth, in context: ModelContext) -> [SavedSpot] {
        let key = width.rawValue
        return (try? context.fetch(FetchDescriptor<SavedSpot>(predicate: #Predicate { $0.layoutWidth == key }))) ?? []
    }
}
