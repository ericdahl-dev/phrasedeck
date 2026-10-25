import Foundation

/// Double-tap confirm for risky phrases: the first tap arms the gate, and
/// a second tap within `window` seconds sends. A later tap just re-arms.
struct ConfirmGate {
    static let window: TimeInterval = {
        #if DEBUG
        // UI tests launch with `-uiTestingConfirmWindow 10` so a slow simulator can't miss it.
        let override = UserDefaults.standard.double(forKey: "uiTestingConfirmWindow")
        if override > 0 { return override }
        #endif
        return 1.5
    }()

    private var armedAt: Date?

    /// Returns true when this tap should send.
    mutating func tap(at now: Date = .now) -> Bool {
        if isArmed(at: now) {
            armedAt = nil
            return true
        }
        armedAt = now
        return false
    }

    func isArmed(at now: Date = .now) -> Bool {
        guard let armedAt else { return false }
        return now.timeIntervalSince(armedAt) <= Self.window
    }

    mutating func disarm() { armedAt = nil }
}
