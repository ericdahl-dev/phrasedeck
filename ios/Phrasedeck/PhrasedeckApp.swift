import SwiftData
import SwiftUI

@main
struct PhrasedeckApp: App {
    // Local only until the sync slice (#10) adds the CloudKit container
    // and the iCloud entitlement.
    let container: ModelContainer = {
        do {
            let container = try ModelContainer(
                for: Page.self, Phrase.self,
                configurations: ModelConfiguration(cloudKitDatabase: .none))
            try Seed.insertMissing(container.mainContext)
            return container
        } catch {
            fatalError("Could not open the phrase store: \(error)")
        }
    }()

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .modelContainer(container)
    }
}
