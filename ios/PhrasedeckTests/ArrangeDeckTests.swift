import Foundation
import SwiftData
import Testing
@testable import Phrasedeck

/// Moving Buttons in Arrange mode, as the accessibility move actions do (#81, spec section 11).
@MainActor
struct ArrangeDeckTests {
    let container: ModelContainer
    var context: ModelContext { container.mainContext }

    init() throws {
        container = try ModelContainer(
            for: Page.self, Phrase.self, SavedSpot.self,
            configurations: ModelConfiguration(isStoredInMemoryOnly: true, cloudKitDatabase: .none))
    }

    func page(_ labels: [String]) throws -> (Page, [Phrase]) {
        let page = try Deck.addPage(named: "Agents", in: context)
        let phrases = try labels.map { try Deck.addPhrase(PhraseDraft(label: $0, text: $0), to: page, in: context) }
        return (page, phrases)
    }

    /// Label -> [x, y] as shown.
    func shown(_ page: Page, columns: Int = 3, width: LayoutWidth = .compact) -> [String: [Int]] {
        let labels = Dictionary(uniqueKeysWithValues: page.sortedPhrases.map { ($0.uid, $0.label) })
        return Dictionary(uniqueKeysWithValues: Deck.layout(of: page, width: width, columns: columns, in: context)
            .map { (labels[$0.id]!, [$0.x, $0.y]) })
    }

    @Test func aPageWithNoSpotsShowsPacked() throws {
        let (page, _) = try page(["a", "b", "c", "d"])

        #expect(shown(page) == ["a": [0, 0], "b": [1, 0], "c": [2, 0], "d": [0, 1]])
    }

    /// The first move saves where every Button is shown, so a later change in order made on another
    /// phone can't disturb this Page (spec section 2, "first commit snapshots the Page").
    @Test func theFirstMoveSnapshotsThePageAndMovesTheButton() throws {
        let (page, p) = try page(["a", "b", "c", "d"])

        let shifted = try Deck.move(p[3], to: Spot(column: 1, row: 1), on: page, width: .compact, columns: 3,
                                    in: context)

        #expect(shifted == 0)
        #expect(shown(page) == ["a": [0, 0], "b": [1, 0], "c": [2, 0], "d": [1, 1]])
        #expect(Deck.spots(on: page, width: .compact, in: context).count == 4)
        #expect(shown(page, width: .regular) == ["a": [0, 0], "b": [1, 0], "c": [2, 0], "d": [0, 1]])
    }

    /// Moving onto another Button shifts it, and says how many moved, for the announcement.
    @Test func movingOntoAButtonShiftsItAndCountsIt() throws {
        let (page, p) = try page(["a", "b", "c"])

        let shifted = try Deck.move(p[2], to: Spot(column: 0, row: 0), on: page, width: .compact, columns: 3,
                                    in: context)

        #expect(shifted == 1)
        #expect(shown(page) == ["c": [0, 0], "b": [1, 0], "a": [2, 0]])
    }

    // MARK: The VoiceOver move actions (spec section 11)

    @Test func moveRightTakesTheNextSquareAndShiftsWhatWasThere() throws {
        let (page, p) = try page(["a", "b", "c", "d"])

        let result = try Deck.nudge(p[0], .right, on: page, width: .compact, columns: 3, in: context)

        #expect(result.to == Spot(column: 1, row: 0))
        #expect(result.shifted == 1)
        #expect(shown(page) == ["a": [1, 0], "b": [1, 1], "c": [2, 0], "d": [0, 1]])
    }

    @Test func moveDownCanOpenANewRow() throws {
        let (page, p) = try page(["a", "b"])

        let result = try Deck.nudge(p[1], .down, on: page, width: .compact, columns: 3, in: context)

        #expect(result.to == Spot(column: 1, row: 1))
        #expect(result.shifted == 0)
        #expect(shown(page) == ["a": [0, 0], "b": [1, 1]])
    }

    @Test func theAnnouncementSaysWhereItWentAndHowManyMoved() {
        #expect(Deck.moveAnnouncement(to: Spot(column: 2, row: 1), shifted: 0) == "Moved to row 2, column 3")
        #expect(Deck.moveAnnouncement(to: Spot(column: 0, row: 0), shifted: 1)
            == "Moved to row 1, column 1. 1 Button moved")
        #expect(Deck.moveAnnouncement(to: Spot(column: 0, row: 0), shifted: 2)
            == "Moved to row 1, column 1. 2 Buttons moved")
    }

    /// Move to Page names the Page itself, so two Pages with the same name can't be mixed up. The
    /// Button goes to the end of that Page (spec rule 8).
    @Test func moveToPageGoesToThatPageEvenWhenTwoShareAName() throws {
        let (page, p) = try page(["a"])
        let twinA = try Deck.addPage(named: "Twin", in: context)
        let twinB = try Deck.addPage(named: "Twin", in: context)
        try Deck.addPhrase(PhraseDraft(label: "x", text: "x"), to: twinB, in: context)

        try Deck.moveToPage(p[0], twinB)

        #expect(twinB.sortedPhrases.map(\.label) == ["x", "a"])
        #expect(twinA.sortedPhrases.isEmpty && page.sortedPhrases.isEmpty)
    }
}
