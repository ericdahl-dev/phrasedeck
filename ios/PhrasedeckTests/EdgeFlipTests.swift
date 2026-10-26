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

/// Near the top or bottom of the visible Page, a drag scrolls it (#81, spec section 5).
struct AutoScrollTests {
    /// The visible Page runs from y 100 to 900 on screen.
    func step(_ y: Double) -> Double { AutoScroll.step(fingerY: y, top: 100, bottom: 900) }

    @Test func inTheMiddleNothingScrolls() {
        #expect(step(500) == 0)
        #expect(step(145) == 0)
        #expect(step(855) == 0)
    }

    @Test func within44PointsOfAnEdgeItScrollsThatWayFasterNearerTheEdge() {
        #expect(step(890) > 0)
        #expect(step(120) < 0)
        #expect(step(899) > step(870))
        #expect(step(101) < step(130))
    }

    /// Past the edge (the finger off the Page) it scrolls at full speed, not faster.
    @Test func pastTheEdgeItScrollsAtFullSpeed() {
        #expect(step(990) == AutoScroll.maxStep)
        #expect(step(10) == -AutoScroll.maxStep)
    }
}
