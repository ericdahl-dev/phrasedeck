import Foundation
import Testing
@testable import Phrasedeck

/// Where a dragged Button is headed, and when that counts (#81, spec section 5).
struct DragTests {
    /// A Square plus spacing is 100 x 80 points here.
    static let step = (width: 100.0, height: 80.0)

    func target(_ x: Double, _ y: Double, columns: Int = 3, width: Int = 1) -> Spot {
        Arrangement.target(topLeft: (x, y), step: Self.step, columns: columns, width: width)
    }

    @Test func theTargetIsTheSquareNearestTheButtonsTopLeftCorner() {
        #expect(target(0, 0) == Spot(column: 0, row: 0))
        #expect(target(149, 39) == Spot(column: 1, row: 0))
        #expect(target(151, 41) == Spot(column: 2, row: 1))
    }

    /// Never off the left or top, and never so far right the Button wouldn't fit. Below the last
    /// row is allowed: that targets new rows.
    @Test func theTargetStaysOnThePage() {
        #expect(target(-80, -300) == Spot(column: 0, row: 0))
        #expect(target(290, 0, width: 2) == Spot(column: 1, row: 0))
        #expect(target(0, 800) == Spot(column: 0, row: 10))
    }

    /// The finger has to rest on a target for 0.15 s before the Page reflows, so a fast sweep doesn't
    /// shuffle it.
    @Test func aTargetCountsAfterTheFingerRestsOnItBriefly() {
        let t0 = Date(timeIntervalSince1970: 0)
        var dwell = Dwell()
        let a = Spot(column: 1, row: 0), b = Spot(column: 2, row: 0)

        dwell.update(a, at: t0)
        #expect(dwell.accepted == nil)
        dwell.update(b, at: t0.addingTimeInterval(0.1))
        dwell.update(b, at: t0.addingTimeInterval(0.2))
        #expect(dwell.accepted == nil, "b has only been held 0.1 s")
        dwell.update(b, at: t0.addingTimeInterval(0.26))
        #expect(dwell.accepted == b)
    }

    @Test func movingOnKeepsTheLastAcceptedTargetUntilTheNextOneCounts() {
        let t0 = Date(timeIntervalSince1970: 0)
        var dwell = Dwell()
        let a = Spot(column: 1, row: 0), b = Spot(column: 2, row: 0)
        dwell.update(a, at: t0)
        dwell.update(a, at: t0.addingTimeInterval(0.2))

        dwell.update(b, at: t0.addingTimeInterval(0.3))

        #expect(dwell.accepted == a)
        #expect(dwell.candidate == b)
    }
}
