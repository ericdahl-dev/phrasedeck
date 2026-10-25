#if DEBUG
import Foundation
import Observation

/// For UI tests (launch argument -uiTestingStubHelper): an in-process
/// helper that answers every call and counts sends, with a throwaway
/// computers list. No network and no Keychain items that outlive the run.
@MainActor
final class StubHelper {
    @MainActor @Observable
    final class Probe {
        var sendCount = 0
    }

    struct API: HelperAPI {
        let probe: Probe

        func health() async throws -> Health {
            Health(hostname: "stub-mac", os: "darwin", accessibilityOK: true)
        }

        func pair(code: String, device: String) async throws -> String { "stub-token" }

        func type(text: String, enter: Bool) async throws {
            await MainActor.run { probe.sendCount += 1 }
        }
    }

    let probe = Probe()
    let defaults: UserDefaults

    init() {
        let suite = "uiTestingStubHelper"
        defaults = UserDefaults(suiteName: suite)!
        defaults.removePersistentDomain(forName: suite)
    }

    func makeModel() -> AppModel {
        let tokens = TokenStore(service: "dev.ericdahl.phrasedeck.uitests")
        let store = HelperStore(defaults: defaults, tokens: tokens)
        let mac = Helper(name: "stub-mac", host: "stub-mac")
        try? store.add(mac)
        store.selectedHelperID = mac.id
        let api = API(probe: probe)
        return AppModel(store: store, tokens: tokens, device: "UI test") { _, _ in api }
    }
}
#endif
