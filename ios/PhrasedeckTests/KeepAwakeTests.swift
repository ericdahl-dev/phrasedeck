import SwiftUI
import Testing
@testable import Phrasedeck

/// The screen stays on only while the app is in front (#159).
struct KeepAwakeTests {
    @Test(arguments: [
        (ScenePhase.active, true),
        (.inactive, false),
        (.background, false),
    ])
    func idleTimerIsOffOnlyWhileActive(phase: ScenePhase, disabled: Bool) {
        #expect(KeepAwake.idleTimerDisabled(for: phase) == disabled)
    }
}
