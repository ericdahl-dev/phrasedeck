import CoreData
import Foundation
import SwiftData

// Every property is defaulted or optional and there are no unique
// constraints, as CloudKit sync requires. `uid` is the same on every device
// a row syncs to, so the seed merge picks the same survivor everywhere.

@Model
final class Page {
    var name: String = ""
    var order: Int = 0
    var seedKey: String?
    var uid: String = ""
    @Relationship(deleteRule: .cascade, inverse: \Phrase.page)
    var phrases: [Phrase]? = []

    init(name: String, order: Int, seedKey: String? = nil) {
        self.name = name
        self.order = order
        self.seedKey = seedKey
        self.uid = UUID().uuidString
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
    var uid: String = ""
    // Stored as span for CloudKit: these sync as CD_spanColumns and CD_spanRows (#77), and
    // renaming them would add new fields that existing Buttons don't have. The defaults keep
    // old phrases 1x1.
    private var spanColumns: Int = 1
    private var spanRows: Int = 1

    /// The Button's Size: how many Squares it covers, 1...3 each way (#51).
    var sizeColumns: Int {
        get { spanColumns }
        set { spanColumns = newValue }
    }
    var sizeRows: Int {
        get { spanRows }
        set { spanRows = newValue }
    }

    init(label: String, text: String, pressEnter: Bool = false, confirm: Bool = false,
         colorName: String = "blue", order: Int, page: Page? = nil, seedKey: String? = nil,
         sizeColumns: Int = 1, sizeRows: Int = 1) {
        self.label = label
        self.text = text
        self.pressEnter = pressEnter
        self.confirm = confirm
        self.colorName = colorName
        self.order = order
        self.page = page
        self.seedKey = seedKey
        self.uid = UUID().uuidString
        self.spanColumns = sizeColumns
        self.spanRows = sizeRows
    }
}

/// The starter deck from the design. Each row has a fixed seed key, and
/// only missing keys are inserted, so a row already synced from another
/// device isn't doubled. It runs once per device: after that, a starter
/// phrase Eric deletes stays deleted. Rows seeded on two devices before
/// either synced are merged by `mergeDuplicates`.
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

    /// Merges starter rows that two devices seeded before syncing: per seed key, the row with
    /// the lowest `uid` stays, so every device deletes the same copies. Phrases on a duplicate
    /// page move to the page that stays. Runs at launch and after each CloudKit import.
    // shortcut: an edit made to a copy that loses is dropped; fine while only seeded rows double.
    @MainActor
    static func mergeDuplicates(_ context: ModelContext) throws {
        let pages = try context.fetch(FetchDescriptor<Page>())
        let phrases = try context.fetch(FetchDescriptor<Phrase>())
        for page in pages where page.uid.isEmpty { page.uid = UUID().uuidString }
        for phrase in phrases where phrase.uid.isEmpty { phrase.uid = UUID().uuidString }

        var touched: [Page] = []
        for group in Dictionary(grouping: pages.filter { $0.seedKey != nil }, by: \.seedKey).values
        where group.count > 1 {
            let sorted = group.sorted { $0.uid < $1.uid }
            let keep = sorted[0]
            for duplicate in sorted.dropFirst() {
                for phrase in duplicate.phrases ?? [] { phrase.page = keep }
                context.delete(duplicate)
            }
            touched.append(keep)
        }

        for group in Dictionary(grouping: phrases.filter { $0.seedKey != nil }, by: \.seedKey).values
        where group.count > 1 {
            for duplicate in group.sorted(by: { $0.uid < $1.uid }).dropFirst() {
                if let page = duplicate.page { touched.append(page) }
                duplicate.page = nil
                context.delete(duplicate)
            }
        }

        // Copies of a starter phrase share an order, so (order, uid) keeps the starter layout.
        for page in touched {
            let sorted = (page.phrases ?? []).sorted { ($0.order, $0.uid) < ($1.order, $1.uid) }
            for (order, phrase) in sorted.enumerated() { phrase.order = order }
        }
        if context.hasChanges { try context.save() }
    }
}

/// Merges duplicate starter rows after each CloudKit import. It lives next to the container
/// (PhrasedeckApp), not in a view, so it runs whichever screen is showing.
@MainActor
final class SyncMerge {
    // Never removed: the app keeps one SyncMerge for its whole life.
    private var observer: NSObjectProtocol?

    init(container: ModelContainer) {
        observer = NotificationCenter.default.addObserver(
            forName: .NSPersistentStoreRemoteChange, object: nil, queue: .main
        ) { _ in
            MainActor.assumeIsolated {
                do { try Seed.mergeDuplicates(container.mainContext) } catch {
                    // Nothing the user can do; the next import or launch tries again.
                    print("phrasedeck: merging synced duplicates failed: \(error)")
                }
            }
        }
    }
}
