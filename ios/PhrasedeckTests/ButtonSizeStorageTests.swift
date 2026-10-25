import Foundation
import SwiftData
import Testing
@testable import Phrasedeck

/// A Button's Size syncs as the CloudKit fields `CD_spanColumns` and `CD_spanRows` (#77).
/// CloudKit names a field after the stored attribute, so those names must never change.
@MainActor
struct ButtonSizeStorageTests {
    @Test func theSizeIsStoredUnderTheSyncedFieldNames() throws {
        let attributes = Set(try #require(Schema([Phrase.self, Page.self]).entitiesByName["Phrase"])
            .attributes.map(\.name))
        #expect(attributes.isSuperset(of: ["spanColumns", "spanRows"]))
        #expect(attributes.isDisjoint(with: ["sizeColumns", "sizeRows", "size"]))
    }

    @Test func aTwoByOneButtonIsStillTwoByOneWhenReadBackFromTheStore() throws {
        let url = FileManager.default.temporaryDirectory.appending(path: "ButtonSize-\(UUID().uuidString).store")
        let container = try ModelContainer(for: Page.self, Phrase.self,
                                           configurations: ModelConfiguration(url: url, cloudKitDatabase: .none))
        let writer = ModelContext(container)
        let page = Page(name: "Agents", order: 0)
        writer.insert(page)
        writer.insert(Phrase(label: "wide", text: "w", order: 0, page: page, sizeColumns: 2, sizeRows: 1))
        try writer.save()

        // A fresh context has nothing cached, so it reads the saved spanColumns and spanRows.
        let phrase = try #require(try ModelContext(container).fetch(FetchDescriptor<Phrase>()).first)
        #expect(phrase.sizeColumns == 2 && phrase.sizeRows == 1)
    }
}
