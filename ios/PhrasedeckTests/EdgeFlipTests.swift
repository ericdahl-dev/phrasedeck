import Foundation
import Testing
@testable import Phrasedeck

/// Holding a dragged Button at the left or right screen edge flips the Page (#81, spec section 6).
struct EdgeFlipTests {
    let t0 = Date(timeIntervalSince1970: 0)
    func at(_ seconds: Double) -> Date { t0.addingTimeInterval(seconds) }

    /// A 400 pt wide screen with Pages 0, 1 and 2, showing Page 1.
    func update(_ flip: inout EdgeFlip, x: Double, _ seconds: Double, page: Int = 1) -> Int? {
        flip.update(x: x, width: 400, page: page, pages: 3, at: at(seconds))
    }

    @Test func holdingAtTheRightEdgeFlipsToTheNextPageAfterAShortHold() {
        var flip = EdgeFlip()

        #expect(update(&flip, x: 390, 0) == nil)
        #expect(update(&flip, x: 390, 0.5) == nil)
        #expect(update(&flip, x: 390, 0.6) == 2)
    }

    @Test func holdingAtTheLeftEdgeFlipsBack() {
        var flip = EdgeFlip()

        _ = update(&flip, x: 10, 0)

        #expect(update(&flip, x: 10, 0.6) == 0)
    }

    /// After a flip, holding on flips again every 0.8 s, from the Page now shown.
    @Test func holdingOnFlipsAgainEvery0Point8Seconds() {
        var flip = EdgeFlip()
        _ = update(&flip, x: 390, 0, page: 0)
        #expect(update(&flip, x: 390, 0.6, page: 0) == 1)

        #expect(update(&flip, x: 390, 0.7, page: 1) == nil)
        #expect(update(&flip, x: 390, 1.35, page: 1) == nil)
        #expect(update(&flip, x: 390, 1.45, page: 1) == 2)
    }

    /// There's no Page past the first or the last, so that edge does nothing.
    @Test func theOuterEdgesOfTheFirstAndLastPageDoNothing() {
        var right = EdgeFlip(), left = EdgeFlip()
        _ = update(&right, x: 390, 0, page: 2)
        _ = update(&left, x: 10, 0, page: 0)

        #expect(update(&right, x: 390, 2, page: 2) == nil)
        #expect(update(&left, x: 10, 2, page: 0) == nil)
    }
}
