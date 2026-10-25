import Testing
@testable import Phrasedeck

/// Showing a Page from its Spots for one Layout width (#81, spec section 2).
struct ArrangementTests {
    typealias Item = Arrangement.Item<Int>

    /// Items in `order`: (columns, rows, Spot as [column, row] or nil).
    static func items(_ list: [(Int, Int, [Int]?)]) -> [Item] {
        list.enumerated().map { i, e in
            Item(id: i, columns: e.0, rows: e.1, spot: e.2.map { Spot(column: $0[0], row: $0[1]) })
        }
    }

    /// (x, y, columns, rows) per id.
    static func shown(_ placements: [Packing.Placement<Int>]) -> [Int: [Int]] {
        Dictionary(uniqueKeysWithValues: placements.map { ($0.id, [$0.x, $0.y, $0.columns, $0.rows]) })
    }

    @Test func aPageWithNoSpotsIsPackedFromOrderAsBefore() {
        let list = Self.items([(1, 1, nil), (2, 2, nil), (1, 1, nil), (1, 1, nil)])
        let packed = Packing.pack(list.map { Packing.Item(id: $0.id, columns: $0.columns, rows: $0.rows) },
                                  columns: 3)

        #expect(Arrangement.resolve(list, columns: 3) == packed)
    }

    @Test func buttonsWithSpotsShowAtThemAndGapsStay() {
        // Out of order on purpose: Spots, not order, decide where Buttons are.
        let list = Self.items([(1, 1, [2, 1]), (2, 1, [0, 0]), (1, 1, [0, 3])])

        let shown = Self.shown(Arrangement.resolve(list, columns: 3))

        #expect(shown == [0: [2, 1, 1, 1], 1: [0, 0, 2, 1], 2: [0, 3, 1, 1]])
    }

    /// Pinched to fewer columns: a Spot past the right edge is pulled in. Nothing is saved, so
    /// pinching back shows it where it was.
    @Test func aSpotPastTheRightEdgeIsPulledIn() {
        let shown = Self.shown(Arrangement.resolve(Self.items([(1, 1, [3, 0])]), columns: 2))

        #expect(shown == [0: [1, 0, 1, 1]])
    }

    /// Placed in reading order of Spots (row, then column): an overlapping Button goes to the first
    /// free Spot at or after its own.
    @Test func anOverlapGoesToTheFirstFreeSpotAtOrAfterItsOwn() {
        // 0 is 2 wide at column 2 of 3, so it's pulled in to column 1, where 1 already is.
        let shown = Self.shown(Arrangement.resolve(Self.items([(2, 1, [2, 0]), (1, 1, [1, 0])]), columns: 3))

        #expect(shown == [1: [1, 0, 1, 1], 0: [0, 1, 2, 1]])
    }

    /// Two Buttons on the same Spot (two phones edited at once): the earlier in order keeps it.
    @Test func onTheSameSpotTheEarlierInOrderKeepsIt() {
        let shown = Self.shown(Arrangement.resolve(Self.items([(1, 1, [1, 1]), (1, 1, [1, 1])]), columns: 3))

        #expect(shown == [0: [1, 1, 1, 1], 1: [2, 1, 1, 1]])
    }

    /// On an arranged Page, Buttons with no Spot (new on another phone, or the "+" tile) go after the
    /// last taken Square, in order. Gaps the user left stay empty.
    @Test func buttonsWithoutASpotGoAfterTheLastTakenSquareNotIntoGaps() {
        let list = Self.items([(1, 1, [0, 0]), (1, 1, nil), (1, 1, [2, 1]), (2, 1, nil)])

        let shown = Self.shown(Arrangement.resolve(list, columns: 3))

        #expect(shown == [0: [0, 0, 1, 1], 2: [2, 1, 1, 1], 1: [0, 2, 1, 1], 3: [1, 2, 2, 1]])
    }

    /// "Last taken" counts every Square a tall Button covers, not just its top-left one.
    @Test func theLastTakenSquareCountsATallButtonsLowerSquares() {
        let list = Self.items([(1, 2, [2, 0]), (1, 1, nil)])

        let shown = Self.shown(Arrangement.resolve(list, columns: 3))

        #expect(shown[1] == [0, 2, 1, 1])
    }

    // MARK: Moving (spec section 5). A move starts from the layout at pickup and returns every
    // Button's Spot after it.

    typealias Placement = Packing.Placement<Int>

    /// Pickup layout from (id, x, y, columns, rows).
    static func layout(_ list: [(Int, Int, Int, Int, Int)]) -> [Placement] {
        list.map { Placement(id: $0.0, x: $0.1, y: $0.2, columns: $0.3, rows: $0.4) }
    }

    static func spots(_ map: [Int: Spot]) -> [Int: [Int]] { map.mapValues { [$0.column, $0.row] } }

    @Test func droppingOnEmptySquaresMovesNothingElse() {
        let pickup = Self.layout([(0, 0, 0, 1, 1), (1, 1, 0, 1, 1), (2, 2, 0, 1, 1)])

        let after = Arrangement.move(0, to: Spot(column: 1, row: 1), from: pickup, columns: 3)

        #expect(Self.spots(after) == [0: [1, 1], 1: [1, 0], 2: [2, 0]])
    }

    /// The dropped Button takes the target; the Button it covers shifts to the first free Spot at
    /// or after its own, which can be the dropped Button's old Spot. Others don't move.
    @Test func droppingOnAButtonShiftsItForward() {
        let pickup = Self.layout([(0, 0, 0, 1, 1), (1, 1, 0, 1, 1), (2, 2, 0, 1, 1), (3, 0, 1, 1, 1)])

        let after = Arrangement.move(3, to: Spot(column: 1, row: 0), from: pickup, columns: 3)

        #expect(Self.spots(after) == [0: [0, 0], 1: [0, 1], 2: [2, 0], 3: [1, 0]])
    }

    /// A 2x2 dropped over 1x1s displaces each one it covers, in reading order of their pickup Spots.
    @Test func aBigButtonDisplacesEachButtonItCovers() {
        let pickup = Self.layout([(0, 0, 0, 1, 1), (1, 1, 0, 1, 1), (2, 2, 0, 1, 1), (3, 0, 1, 1, 1),
                                  (4, 0, 2, 2, 2)])

        let after = Arrangement.move(4, to: Spot(column: 0, row: 0), from: pickup, columns: 3)

        #expect(Self.spots(after) == [4: [0, 0], 2: [2, 0], 0: [2, 1], 1: [0, 2], 3: [1, 2]])
    }

    /// Moving back to where it started puts every neighbor back.
    @Test func droppingBackOnThePickupSpotChangesNothing() {
        let pickup = Self.layout([(0, 0, 0, 1, 1), (1, 1, 0, 2, 1), (2, 0, 1, 1, 1)])

        let after = Arrangement.move(1, to: Spot(column: 1, row: 0), from: pickup, columns: 3)

        #expect(Self.spots(after) == [0: [0, 0], 1: [1, 0], 2: [0, 1]])
    }

    /// A target past the right edge is pulled in so the whole Button fits.
    @Test func aTargetPastTheRightEdgeIsPulledIn() {
        let pickup = Self.layout([(0, 0, 0, 2, 1)])

        let after = Arrangement.move(0, to: Spot(column: 2, row: 3), from: pickup, columns: 3)

        #expect(Self.spots(after) == [0: [1, 3]])
    }
}
