import SwiftUI
import Testing
@testable import Phrasedeck

struct DeckGridTests {
    @Test func theIPhoneHasThreeColumns() {
        #expect(DeckGrid.columns(for: .compact).count == 3)
    }

    @Test func theIPadAdaptsWithAComfortableMinimumWidth() throws {
        let columns = DeckGrid.columns(for: .regular)
        #expect(columns.count == 1)
        guard case .adaptive(let minimum, _) = try #require(columns.first).size else {
            Issue.record("iPad column isn't adaptive")
            return
        }
        #expect(minimum >= DeckGrid.minimumButtonWidth)
    }
}
