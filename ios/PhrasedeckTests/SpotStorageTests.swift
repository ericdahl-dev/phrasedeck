import Foundation
import SwiftData
import Testing
@testable import Phrasedeck

/// Spots are saved per Button and per Layout width, and sync (#81, spec section 12).
@MainActor
struct SpotStorageTests {
    let container: ModelContainer
    var context: ModelContext { container.mainContext }

    init() throws {
        container = try ModelContainer(
            for: Page.self, Phrase.self, SavedSpot.self,
            configurations: ModelConfiguration(isStoredInMemoryOnly: true, cloudKitDatabase: .none))
    }

    func page(_ name: String, _ labels: [String]) throws -> (Page, [Phrase]) {
        let page = try Deck.addPage(named: name, in: context)
        let phrases = try labels.map { try Deck.addPhrase(PhraseDraft(label: $0, text: $0), to: page, in: context) }
        return (page, phrases)
    }

    /// Spots sync as a new record type only. The Page and Phrase records keep exactly the fields
    /// they had, because CloudKit names each field after its stored property (CD_<name>).
    @Test func spotsAddARecordTypeAndLeaveThePageAndPhraseFieldsAlone() throws {
        let entities = Schema([Page.self, Phrase.self, SavedSpot.self]).entitiesByName
        func names(_ entity: String) throws -> Set<String> {
            Set(try #require(entities[entity]).properties.map(\.name))
        }

        #expect(try names("Page") == ["name", "order", "seedKey", "uid", "phrases"])
        #expect(try names("Phrase") == ["label", "text", "pressEnter", "confirm", "colorName", "order", "page",
                                        "seedKey", "uid", "spanColumns", "spanRows"])
        #expect(try names("SavedSpot") == ["phraseUID", "layoutWidth", "column", "row", "updated"])
    }

    @Test func savedSpotsReadBackForTheirOwnLayoutWidthOnly() throws {
        let (page, p) = try page("Agents", ["a", "b"])

        try Deck.saveSpots([p[0].uid: Spot(column: 2, row: 0), p[1].uid: Spot(column: 0, row: 3)],
                           width: .compact, in: context)

        #expect(Deck.spots(on: page, width: .compact, in: context)
            == [p[0].uid: Spot(column: 2, row: 0), p[1].uid: Spot(column: 0, row: 3)])
        #expect(Deck.spots(on: page, width: .regular, in: context).isEmpty)
    }

    func rows(_ uid: String) throws -> [SavedSpot] {
        try context.fetch(FetchDescriptor<SavedSpot>()).filter { $0.phraseUID == uid }
    }

    @Test func savingAgainMovesTheSpotWithoutAddingARow() throws {
        let (page, p) = try page("Agents", ["a"])

        try Deck.saveSpots([p[0].uid: Spot(column: 0, row: 0)], width: .compact, in: context)
        try Deck.saveSpots([p[0].uid: Spot(column: 1, row: 2)], width: .compact, in: context)

        #expect(Deck.spots(on: page, width: .compact, in: context) == [p[0].uid: Spot(column: 1, row: 2)])
        #expect(try rows(p[0].uid).count == 1)
    }

    /// Saving a Spot that didn't change leaves its row alone, so it can't undo a newer move synced
    /// from another phone.
    @Test func anUnchangedSpotIsNotRewritten() throws {
        let (_, p) = try page("Agents", ["a", "b"])
        try Deck.saveSpots([p[0].uid: Spot(column: 0, row: 0)], width: .compact, in: context)
        let before = try #require(try rows(p[0].uid).first).updated

        try Deck.saveSpots([p[0].uid: Spot(column: 0, row: 0), p[1].uid: Spot(column: 1, row: 0)],
                           width: .compact, in: context)

        #expect(try #require(try rows(p[0].uid).first).updated == before)
    }

    /// Two phones that each saved a Spot for the same Button sync as two rows: the newest wins, and
    /// the next save leaves one.
    /// Rows saved at the same instant settle the same way on every phone, whatever order the store
    /// returns them in: the earlier Spot in reading order wins.
    @Test(arguments: [false, true])
    func rowsWithTheSameTimeSettleTheSameWayEverywhere(reversed: Bool) throws {
        let (page, p) = try page("Agents", ["a"])
        let at = Date(timeIntervalSince1970: 100)
        var synced = [SavedSpot(phraseUID: p[0].uid, width: .compact, spot: Spot(column: 0, row: 5)),
                      SavedSpot(phraseUID: p[0].uid, width: .compact, spot: Spot(column: 2, row: 1))]
        if reversed { synced.reverse() }
        for row in synced {
            row.updated = at
            context.insert(row)
        }
        try context.save()

        #expect(Deck.spots(on: page, width: .compact, in: context) == [p[0].uid: Spot(column: 2, row: 1)])
        try Deck.saveSpots([:], width: .compact, in: context)
        try Deck.saveSpots([p[0].uid: Spot(column: 2, row: 1)], width: .compact, in: context)
        #expect(try rows(p[0].uid).map(\.spot) == [Spot(column: 2, row: 1)])
    }

    @Test func ofTwoSyncedRowsTheNewestWinsAndTheNextSaveLeavesOne() throws {
        let (page, p) = try page("Agents", ["a"])
        let old = SavedSpot(phraseUID: p[0].uid, width: .compact, spot: Spot(column: 0, row: 5))
        let new = SavedSpot(phraseUID: p[0].uid, width: .compact, spot: Spot(column: 2, row: 1))
        context.insert(new)
        context.insert(old)
        // Dates are set after insert, as the app does: set before, they were lost on iOS 26 (#90).
        old.updated = Date(timeIntervalSince1970: 100)
        new.updated = Date(timeIntervalSince1970: 200)
        try context.save()

        // Flaky on CI's iOS 26.4.1 Simulator (#90): the message shows each row's date when it fails.
        let saved = try rows(p[0].uid).map { "\($0.spot) @ \($0.updated.timeIntervalSince1970)" }
        #expect(Deck.spots(on: page, width: .compact, in: context) == [p[0].uid: Spot(column: 2, row: 1)],
                "rows: \(saved)")

        try Deck.saveSpots([p[0].uid: Spot(column: 0, row: 0)], width: .compact, in: context)
        #expect(try rows(p[0].uid).map(\.spot) == [Spot(column: 0, row: 0)])
    }

    func spotted(_ phrases: [Phrase]) throws {
        for width in LayoutWidth.allCases {
            try Deck.saveSpots(Dictionary(uniqueKeysWithValues: phrases.enumerated().map {
                ($0.element.uid, Spot(column: $0.offset, row: 0))
            }), width: width, in: context)
        }
    }

    @Test func deletingAButtonDeletesItsSpotsOnEveryLayoutWidth() throws {
        let (_, p) = try page("Agents", ["a", "b"])
        try spotted(p)

        try Deck.delete(p[0], in: context)

        #expect(try rows(p[0].uid).isEmpty)
        #expect(try rows(p[1].uid).count == LayoutWidth.allCases.count)
    }

    /// A Button moved to another Page loses its Spots, so it shows at the end there (spec rule 8).
    @Test func aButtonMovedToAnotherPageLosesItsSpots() throws {
        let (_, p) = try page("Agents", ["a"])
        let (other, _) = try page("Git", [])
        try spotted(p)

        var draft = PhraseDraft(p[0])
        draft.page = other
        try Deck.save(draft, into: p[0])

        #expect(try rows(p[0].uid).isEmpty)
    }

    @Test func aButtonEditedOnItsOwnPageKeepsItsSpots() throws {
        let (_, p) = try page("Agents", ["a"])
        try spotted(p)

        var draft = PhraseDraft(p[0])
        draft.label = "renamed"
        try Deck.save(draft, into: p[0])

        #expect(try rows(p[0].uid).count == LayoutWidth.allCases.count)
    }

    @Test func deletingAPageDeletesTheSpotsOfItsButtons() throws {
        let (agents, p) = try page("Agents", ["a", "b"])
        let (_, q) = try page("Git", ["c"])
        try spotted(p + q)

        try Deck.delete(agents, in: context)

        #expect(try context.fetch(FetchDescriptor<SavedSpot>()).map(\.phraseUID).allSatisfy { $0 == q[0].uid })
        #expect(try rows(q[0].uid).count == LayoutWidth.allCases.count)
    }
}
