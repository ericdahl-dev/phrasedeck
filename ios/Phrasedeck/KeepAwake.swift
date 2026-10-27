import SwiftUI

/// Keeps the screen on while the app is in front, so the deck is ready to tap (#159).
enum KeepAwake {
    static func idleTimerDisabled(for phase: ScenePhase) -> Bool { phase == .active }
}
