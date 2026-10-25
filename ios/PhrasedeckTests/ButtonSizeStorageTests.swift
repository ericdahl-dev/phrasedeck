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

    @Test func aTwoByOneButtonIsStillTwoByOneWhenTheStoreIsOpenedAgain() throws {
        let url = FileManager.default.temporaryDirectory
            .appending(path: "ButtonSize-\(UUID().uuidString).store")
        defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()
            .appending(path: url.lastPathComponent)) }
        func open() throws -> ModelContainer {
            try ModelContainer(for: Page.self, Phrase.self,
                               configurations: ModelConfiguration(url: url, cloudKitDatabase: .none))
        }

        do {
            let context = try open().mainContext
            let page = Page(name: "Agents", order: 0)
            context.insert(page)
            context.insert(Phrase(label: "wide", text: "w", order: 0, page: page, sizeColumns: 2, sizeRows: 1))
            try context.save()
        }

        let phrase = try #require(try open().mainContext.fetch(FetchDescriptor<Phrase>()).first)
        #expect(phrase.sizeColumns == 2 && phrase.sizeRows == 1)
    }
}
