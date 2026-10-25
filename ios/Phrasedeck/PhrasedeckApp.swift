import SwiftData
import SwiftUI
import UIKit

@main
struct PhrasedeckApp: App {
    @State private var model: AppModel
    private let container: ModelContainer
    private let stub: StubHelper?

    init() {
        #if DEBUG
        let stub = ProcessInfo.processInfo.arguments.contains("-uiTestingStubHelper") ? StubHelper() : nil
        #else
        let stub: StubHelper? = nil
        #endif
        self.stub = stub
        // Local only until the sync slice (#10) adds the CloudKit container
        // and the iCloud entitlement. UI tests get a throwaway store.
        do {
            container = try ModelContainer(
                for: Page.self, Phrase.self,
                configurations: ModelConfiguration(isStoredInMemoryOnly: stub != nil, cloudKitDatabase: .none))
            try Seed.insertMissing(container.mainContext, defaults: stub?.defaults ?? .standard)
        } catch {
            fatalError("Could not open the phrase store: \(error)")
        }
        if let stub {
            _model = State(initialValue: stub.makeModel())
        } else {
            _model = State(initialValue: AppModel(
                store: HelperStore(), tokens: TokenStore(), device: UIDevice.current.name,
                makeClient: { helper, token in HelperClient(helper: helper, token: token) }))
        }
    }

    var body: some Scene {
        WindowGroup {
            ContentView(stubProbe: stub?.probe)
                .environment(model)
        }
        .modelContainer(container)
    }
}
