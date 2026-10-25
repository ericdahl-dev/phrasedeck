import SwiftUI
import Testing
@testable import Phrasedeck

struct DeckSizeTests {
    @Test func theIPhoneGoesFromTwoToFiveColumnsAndStartsAtThree() {
        #expect(DeckSize.range(for: .compact) == 2...5)
        #expect(DeckSize.defaultColumns(for: .compact, width: 393) == 3)
    }

    @Test func theIPadGoesFromThreeToEightAndStartsAtTodaysCount() {
        #expect(DeckSize.range(for: .regular) == 3...8)
        // The old adaptive grid: as many 150-point columns as fit.
        #expect(DeckSize.defaultColumns(for: .regular, width: 834) == 5)
        #expect(DeckSize.defaultColumns(for: .regular, width: 1194) == 7)
        #expect(DeckSize.defaultColumns(for: .regular, width: 320) == 3)
    }

    @Test func pinchingOutMeansFewerBiggerButtons() {
        #expect(DeckSize.columns(pinch: 1.0, from: 3, in: 2...5) == 3)
        #expect(DeckSize.columns(pinch: 1.6, from: 3, in: 2...5) == 2)
        #expect(DeckSize.columns(pinch: 0.7, from: 3, in: 2...5) == 4)
        #expect(DeckSize.columns(pinch: 0.55, from: 3, in: 2...5) == 5)
    }

    @Test func pinchStaysWithinTheLimits() {
        #expect(DeckSize.columns(pinch: 10, from: 3, in: 2...5) == 2)
        #expect(DeckSize.columns(pinch: 0.1, from: 3, in: 2...5) == 5)
        #expect(DeckSize.columns(pinch: 0, from: 3, in: 2...5) == 5)
    }

    @Test func aStoredSizeOutsideTheLimitsFallsBackToTheDefault() {
        #expect(DeckSize.columns(stored: 0, default: 3, in: 2...5) == 3)
        #expect(DeckSize.columns(stored: 9, default: 3, in: 2...5) == 3)
        #expect(DeckSize.columns(stored: 4, default: 3, in: 2...5) == 4)
    }

    @Test func labelsAndButtonsGrowWithTheColumnWidth() {
        let small = DeckSize.metrics(columnWidth: 60), big = DeckSize.metrics(columnWidth: 170)
        #expect(small.font < big.font)
        #expect(small.height < big.height)
        #expect(small.height >= 56)  // still a comfortable tap target at 5 columns
        #expect(small.font >= 13)
    }

    @Test func eachSizeClassHasItsOwnKey() {
        #expect(DeckSize.key(for: .compact) != DeckSize.key(for: .regular))
    }

    /// A bigger button gets bigger text, limited by its shorter side (#51).
    @Test func labelsGrowWithTheButtonsSmallerSide() {
        let one = DeckSize.metrics(button: CGSize(width: 100, height: 86))
        let wide = DeckSize.metrics(button: CGSize(width: 312, height: 86))
        let square = DeckSize.metrics(button: CGSize(width: 212, height: 184))
        #expect(wide.font == one.font)
        #expect(square.font > one.font)
        #expect(square.height == 184)
    }
}
