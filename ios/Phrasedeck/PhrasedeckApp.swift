import SwiftData
import SwiftUI
import UIKit

@main
struct PhrasedeckApp: App {
    @State private var model: AppModel
    private let container: ModelContainer
    private let syncMerge: SyncMerge
    private let stub: StubHelper?
    /// Bonjour discovery (#11); nil under UI tests, so no local-network prompt.
    @State private var browser: NearbyBrowser?

    init() {
        #if DEBUG
        let stub = ProcessInfo.processInfo.arguments.contains("-uiTestingStubHelper") ? StubHelper() : nil
        #else
        let stub: StubHelper? = nil
        #endif
        self.stub = stub
        // Phrases and pages sync through the private CloudKit database once the build has the
        // iCloud entitlement (project.yml); computers and tokens live outside this store.
        // UI tests get a throwaway local store.
        let syncs = !((Bundle.main.object(forInfoDictionaryKey: "PhrasedeckCloudSync") as? String) ?? "").isEmpty
        do {
            container = try ModelContainer(
                for: Page.self, Phrase.self,
                configurations: ModelConfiguration(
                    isStoredInMemoryOnly: stub != nil,
                    cloudKitDatabase: syncs && stub == nil ? .private("iCloud.dev.ericdahl.phrasedeck") : .none))
            try Seed.insertMissing(container.mainContext, defaults: stub?.defaults ?? .standard)
            try Seed.mergeDuplicates(container.mainContext)
        } catch {
            fatalError("Could not open the phrase store: \(error)")
        }
        // The other device's starter deck can arrive after this one seeded its own.
        syncMerge = SyncMerge(container: container)
        if let stub {
            _model = State(initialValue: stub.makeModel())
        } else {
            _model = State(initialValue: AppModel(
                store: HelperStore(), tokens: TokenStore(), device: UIDevice.current.name,
                makeClient: { helper, token, route in HelperClient(helper: helper, token: token, route: route) }))
        }
    }

    var body: some Scene {
        WindowGroup {
            ContentView(stubProbe: stub?.probe)
                .environment(model)
                .task {
                    guard stub == nil, browser == nil else { return }
                    let model = model
                    browser = NearbyBrowser { model.setNearby($0) }
                    browser?.start()
                }
        }
        .modelContainer(container)
        // UI tests keep the button size in their throwaway defaults.
        .defaultAppStorage(stub?.defaults ?? .standard)
    }
}
