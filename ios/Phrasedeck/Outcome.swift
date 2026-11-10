/// What a failed send or pairing means to the user, worded once (#44). Status codes become
/// `HelperClient.Failure` in HelperClient; this turns those into what the banner or the
/// pairing sheet shows. Their meaning matches the helper's table in the design doc: 409 and
/// 429 typed nothing, 500 and a lost reply may have typed.
enum Outcome {
    typealias Feedback = AppModel.Feedback

    static func send(_ failure: HelperClient.Failure, on helper: Helper) -> Feedback {
        let (tone, why) = reason(failure, on: helper)
        return Feedback(tone: tone, message: "\(tone.word): \(why)", pairAgain: failure == .unauthorized ? helper : nil)
    }

    /// Why a send failed, without the leading word, so pairing can reuse it.
    private static func reason(_ failure: HelperClient.Failure, on helper: Helper) -> (Feedback.Tone, String) {
        let name = helper.name
        switch failure {
        case .noReply: return (.amber, "no reply from \(name).")
        case .busy: return (.red, "\(name) is busy. Tap again.")
        case .failedPartway: return (.amber, "\(name) failed partway.")
        case .unreachable: return (.red, "\(name) unreachable")
        case .unauthorized: return (.red, "\(name) refused this device. Pair again.")
        case .notReady: return (.red, "\(name) can't type yet: grant Accessibility to phrased")
        case .rateLimited: return (.red, "too many sends. Wait a moment.")
        case .notFound: return (.red, "\(name) failed (error 404)")
        case let .server(code): return (.red, "\(name) failed (error \(code))")
        case .notTailnet: return (.red, "\(name) isn't a tailnet name. Pair it again by scanning its QR code.")
        case let .notSent(code): return (.red, "the connection to \(name) was blocked (error \(code)).")
        case .pinMismatch: return (.red, "\(name)'s certificate didn't match. Scan its QR code to pair again.")
        }
    }

    /// Pairing reads 404, 401, 429, 500 and a lost reply its own way; anything else as a send would.
    static func pair(_ failure: HelperClient.Failure, on helper: Helper) -> String {
        switch failure {
        case .notFound: "Run phrased pair on \(helper.name) first."
        case .unauthorized: "That code didn't match. Check it, or run phrased pair again."
        // Pairing never types: a lost reply means the code may be used up, nothing more.
        case .noReply: "\(helper.name) didn't answer. Run phrased pair again and retry."
        case .failedPartway: "Pairing with \(helper.name) failed. Run phrased pair again."
        case .rateLimited: "Too many pairing attempts. Run phrased pair on \(helper.name) again."
        case let .notSent(code): "\(helper.name): the connection was blocked before anything was sent (error \(code))."
        default: reason(failure, on: helper).1
        }
    }

    /// A Send over the Bluetooth route that didn't end Typed (#202). Pair again is the Helper's
    /// sheet, so a dongle's banner never offers it: the dongle is removed and added again.
    static func send(_ failure: Dongle.Failure, on computer: Helper) -> Feedback {
        let (tone, why) = reason(failure, named: computer.name)
        return Feedback(tone: tone, message: "\(tone.word): \(why)")
    }

    private static func reason(_ failure: Dongle.Failure, named name: String) -> (Feedback.Tone, String) {
        switch failure {
        case .bluetoothOff: return (.red, "Bluetooth is off.")
        case .bluetoothDenied: return (.red, "allow Bluetooth for phrasedeck in Settings.")
        case .notInRange: return (.red, "\(name) isn't in range.")
        case .notPaired: return (.red, "\(name) isn't paired with this phone. Remove it and add it again.")
        case .noToken: return (.red, "this phone has no Token for \(name). Remove it and add it again.")
        case let .writeTooShort(length): return (.red, "\(name)'s Bluetooth writes are too short (\(length) bytes).")
        case .lost: return (.amber, "the Bluetooth link to \(name) dropped.")
        case .noResult: return (.amber, "no reply from \(name).")
        case let .result(code): return reason(code, named: name)
        }
    }

    /// The dongle's Result codes, as the table in `dongle/README.md` reads them.
    private static func reason(_ code: Dongle.Code, named name: String) -> (Feedback.Tone, String) {
        switch code {
        case .typed: return (.success, "\(name) typed it.")
        case .stoppedPartway: return (.amber, "\(name) stopped partway.")
        case .notStarted: return (.red, "\(name) isn't plugged in to a Computer, or was stopped before the first key.")
        case let .cantType(character): return (.red, "a US keyboard can't type “\(character)”.")
        case .wrongToken: return (.red, "\(name) refused this phone's Token. Remove it and add it again.")
        case .busy: return (.red, "\(name) is busy. Tap again.")
        case .badFrame: return (.red, "\(name) couldn't read the Send (error 6).")
        case .tooLong: return (.red, "the Phrase is longer than \(Dongle.maxPhrase) bytes.")
        case let .unknown(raw): return (.red, "\(name) failed (error \(raw)).")
        }
    }

    /// Adding a dongle that didn't work out (#202).
    static func pairDongle(_ failure: Dongle.Failure, named name: String) -> String {
        switch failure {
        case .noToken:
            return "\(name) shows its Token only in the 2 minutes after pairing. Hold BOOT on it for 5 seconds, forget it in Settings > Bluetooth, and add it again."
        case .notPaired:
            return "Pairing with \(name) didn't finish. Check the passkey, or forget it in Settings > Bluetooth and try again."
        case .notInRange, .lost, .noResult:
            return "Couldn't reach \(name). Check that it's plugged in, and try again."
        default:
            let why = reason(failure, named: name).1
            return why.prefix(1).uppercased() + why.dropFirst()
        }
    }

    // Pairing problems the app finds before or after asking the helper.
    static let badCode = "Enter the 6-digit code from phrased pair."
    static func qrMismatch(on helper: Helper) -> String {
        "\(helper.name)'s certificate didn't match the QR code, so it wasn't added. Run phrased pair again and rescan."
    }
    static func pinSaveFailed(on helper: Helper, _ error: any Error) -> String {
        "Paired, but couldn't save \(helper.name)'s certificate: \(error.localizedDescription)"
    }
}
