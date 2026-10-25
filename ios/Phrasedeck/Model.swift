import Foundation
import SwiftData

// Every property is defaulted or optional, and there are no unique
// constraints, so switching on CloudKit sync (#10) needs no migration.

@Model
final class Page {
    var name: String = ""
    var order: Int = 0
    var seedKey: String?
    @Relationship(deleteRule: .cascade, inverse: \Phrase.page)
    var phrases: [Phrase]? = []

    init(name: String, order: Int, seedKey: String? = nil) {
        self.name = name
        self.order = order
        self.seedKey = seedKey
    }

    var sortedPhrases: [Phrase] { (phrases ?? []).sorted { $0.order < $1.order } }
}

@Model
final class Phrase {
    var label: String = ""
    var text: String = ""
    var pressEnter: Bool = false
    var confirm: Bool = false
    var colorName: String = "blue"
    var order: Int = 0
    var page: Page?
    var seedKey: String?

    init(label: String, text: String, pressEnter: Bool = false, confirm: Bool = false,
         colorName: String = "blue", order: Int, page: Page? = nil, seedKey: String? = nil) {
        self.label = label
        self.text = text
        self.pressEnter = pressEnter
        self.confirm = confirm
        self.colorName = colorName
        self.order = order
        self.page = page
        self.seedKey = seedKey
    }
}

/// The starter deck from the design. Each row has a fixed seed key, and
/// only missing keys are inserted, so a row already synced from another
/// device isn't doubled. It runs once per device: after that, a starter
/// phrase Eric deletes stays deleted.
enum Seed {
    static let doneKey = "seed.starterDeck.done"

    struct Row {
        let key, label, text: String
        let pressEnter, confirm: Bool
        let colorName: String
    }

    static let pageKey = "page.agents"
    static let rows: [Row] = [
        Row(key: "phrase.agent-status", label: "Agent status",
            text: "what is the status of the agents? ask them if needed",
            pressEnter: true, confirm: false, colorName: "blue"),
        Row(key: "phrase.status-table", label: "Status table",
            text: "what is the status of the agents? ask them if needed.  show me in a table",
            pressEnter: true, confirm: false, colorName: "blue"),
        Row(key: "phrase.merge-prefix", label: "Merge…", text: "merge ",
            pressEnter: false, confirm: false, colorName: "purple"),
        Row(key: "phrase.yes", label: "Yes", text: "yes",
            pressEnter: true, confirm: true, colorName: "green"),
        Row(key: "phrase.merge-it", label: "Merge it", text: "Merge it",
            pressEnter: true, confirm: true, colorName: "orange"),
    ]

    static func insertMissing(_ context: ModelContext, defaults: UserDefaults = .standard) throws {
        guard !defaults.bool(forKey: doneKey) else { return }

        let pages = try context.fetch(FetchDescriptor<Page>(predicate: #Predicate { $0.seedKey == pageKey }))
        let page = pages.first ?? {
            let page = Page(name: "Agents", order: 0, seedKey: pageKey)
            context.insert(page)
            return page
        }()

        let present = Set(try context.fetch(FetchDescriptor<Phrase>()).compactMap(\.seedKey))
        for (order, row) in rows.enumerated() where !present.contains(row.key) {
            context.insert(Phrase(label: row.label, text: row.text, pressEnter: row.pressEnter,
                                  confirm: row.confirm, colorName: row.colorName, order: order,
                                  page: page, seedKey: row.key))
        }

        try context.save()
        defaults.set(true, forKey: doneKey)
    }
}
