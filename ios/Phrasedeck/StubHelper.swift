import Foundation
import Observation

/// For UI tests (launch argument -uiTestingStubHelper, DEBUG builds only): an in-process
/// helper that answers every call and counts sends, with a throwaway
/// computers list. No network and no Keychain items.
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

        func pair(code: String, device: String) async throws -> Pairing { Pairing(token: "stub-token") }

        func info() async throws -> LANInfo { LANInfo() }

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
        let api = API(probe: probe)
        let computers = Computers(defaults: defaults, tokens: MemoryTokens()) { _, _, _ in api }
        let mac = Helper(name: "stub-mac", host: "stub-mac")
        try? computers.add(mac)
        computers.selectedID = mac.id
        return AppModel(computers: computers, device: "UI test")
    }
}
