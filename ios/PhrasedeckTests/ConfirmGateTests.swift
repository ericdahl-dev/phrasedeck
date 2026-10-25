import Foundation
import Testing
@testable import Phrasedeck

struct ConfirmGateTests {
    let start = Date(timeIntervalSinceReferenceDate: 0)

    @Test func theFirstTapArmsAndTheSecondSends() {
        var gate = ConfirmGate()

        #expect(gate.tap(at: start) == false)
        #expect(gate.isArmed(at: start.addingTimeInterval(1)))
        #expect(gate.tap(at: start.addingTimeInterval(1)) == true)
        #expect(!gate.isArmed(at: start.addingTimeInterval(1)))
    }

    @Test func aSecondTapAfterTheWindowOnlyArmsAgain() {
        var gate = ConfirmGate()

        _ = gate.tap(at: start)
        #expect(!gate.isArmed(at: start.addingTimeInterval(1.6)))
        #expect(gate.tap(at: start.addingTimeInterval(1.6)) == false)
        #expect(gate.tap(at: start.addingTimeInterval(2.0)) == true)
    }

    @Test func aThirdQuickTapStartsOver() {
        var gate = ConfirmGate()

        _ = gate.tap(at: start)
        _ = gate.tap(at: start.addingTimeInterval(0.5))

        #expect(gate.tap(at: start.addingTimeInterval(0.8)) == false)
    }
}
