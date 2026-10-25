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

    var spot: Spot { Spot(column: column, row: row) }
}

extension Deck {
    /// The Spots of the page's Buttons at `width`, by Phrase uid. A Button with no entry has no Spot.
    static func spots(on page: Page, width: LayoutWidth, in context: ModelContext) -> [String: Spot] {
        let uids = Set(page.sortedPhrases.map(\.uid))
        return newest(width, in: context).filter { uids.contains($0.key) }.mapValues(\.spot)
    }

    /// Saves Spots at `width`, by Phrase uid. Only Spots that changed are written, and duplicate
    /// rows for a Button are folded into one.
    static func saveSpots(_ spots: [String: Spot], width: LayoutWidth, in context: ModelContext) throws {
        let rows = Dictionary(grouping: saved(width, in: context), by: \.phraseUID)
        for (uid, spot) in spots {
            let existing = (rows[uid] ?? []).sorted { $0.updated > $1.updated }
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

    /// Deletes the Buttons' Spots on every Layout width. The caller saves.
    static func deleteSpots(of phrases: [Phrase], in context: ModelContext) {
        let uids = Set(phrases.map(\.uid))
        for row in (try? context.fetch(FetchDescriptor<SavedSpot>())) ?? [] where uids.contains(row.phraseUID) {
            context.delete(row)
        }
    }

    /// The newest row per Phrase uid at `width`.
    private static func newest(_ width: LayoutWidth, in context: ModelContext) -> [String: SavedSpot] {
        Dictionary(saved(width, in: context).map { ($0.phraseUID, $0) },
                   uniquingKeysWith: { $0.updated >= $1.updated ? $0 : $1 })
    }

    private static func saved(_ width: LayoutWidth, in context: ModelContext) -> [SavedSpot] {
        let key = width.rawValue
        return (try? context.fetch(FetchDescriptor<SavedSpot>(predicate: #Predicate { $0.layoutWidth == key }))) ?? []
    }
}
