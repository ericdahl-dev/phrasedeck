/// What a Send or a pairing means to the user, worded once (#44). The Courier decides whether a
/// Send was Typed, Maybe typed or Not typed, and why (#206); this only turns that into what the
/// banner or the pairing sheet shows.
enum Outcome {
    typealias Feedback = AppModel.Feedback

    static func send(_ delivery: Courier.Delivery, on computer: Helper) -> Feedback {
        switch delivery {
        case let .typed(reason):
            // Typed over the tailnet after the home network showed another certificate (#11).
            guard reason == .helper(.pinMismatch) else { return Feedback(tone: .success, message: nil) }
            return Feedback(tone: .success, message: "Typed: sent over the tailnet. On the home network, \(computer.name) showed a different certificate. Scan its QR code to pair again.",
                            pairAgain: computer)
        case let .maybeTyped(reason): return worded(.amber, reason, on: computer)
        case let .notTyped(reason): return worded(.red, reason, on: computer)
        }
    }

    /// The word, then why. Pair again is the Helper's sheet, so only a Helper's refusal offers
    /// it; a dongle is removed and added again instead.
    private static func worded(_ tone: Feedback.Tone, _ reason: Courier.Reason, on computer: Helper) -> Feedback {
        Feedback(tone: tone, message: "\(tone.word): \(why(reason, on: computer))",
                 pairAgain: reason == .helper(.unauthorized) ? computer : nil)
    }

    private static func why(_ reason: Courier.Reason, on computer: Helper) -> String {
        switch reason {
        case let .helper(failure): return self.reason(failure, on: computer)
        case let .dongle(failure): return self.reason(failure, named: computer.name)
        case let .unrecognized(description): return "\(computer.name): \(description)"
        case let .inFlight(sending): return "\(sending.name) is busy. Tap again."
        }
    }

    /// Why a send failed, without the leading word, so pairing can reuse it. Their meaning
    /// matches the helper's table in the design doc: 409 and 429 typed nothing, 500 and a lost
    /// reply may have typed.
    private static func reason(_ failure: HelperClient.Failure, on helper: Helper) -> String {
        let name = helper.name
        switch failure {
        case .noReply: return "no reply from \(name)."
        case .busy: return "\(name) is busy. Tap again."
        case .failedPartway: return "\(name) failed partway."
        case .unreachable: return "\(name) unreachable"
        case .unauthorized: return "\(name) refused this device. Pair again."
        case .notReady: return "\(name) can't type yet: grant Accessibility to phrased"
        case .rateLimited: return "too many sends. Wait a moment."
        case .notFound: return "\(name) failed (error 404)"
        case let .server(code): return "\(name) failed (error \(code))"
        case .notTailnet: return "\(name) isn't a tailnet name. Pair it again by scanning its QR code."
        case let .notSent(code): return "the connection to \(name) was blocked (error \(code))."
        case .pinMismatch: return "\(name)'s certificate didn't match. Scan its QR code to pair again."
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
        default: reason(failure, on: helper)
        }
    }

    /// Why a Send over the Bluetooth route (#202) didn't end Typed.
    private static func reason(_ failure: Dongle.Failure, named name: String) -> String {
        switch failure {
        case .bluetoothOff: return "Bluetooth is off."
        case .bluetoothDenied: return "allow Bluetooth for phrasedeck in Settings."
        case .notInRange: return "\(name) isn't in range."
        case .notPaired: return "\(name) isn't paired with this phone. Remove it and add it again."
        case .noToken: return "this phone has no Token for \(name). Remove it and add it again."
        case let .writeTooShort(length): return "\(name)'s Bluetooth writes are too short (\(length) bytes)."
        case .lost: return "the Bluetooth link to \(name) dropped."
        case .noResult: return "no reply from \(name)."
        case let .result(code): return reason(code, named: name)
        }
    }

    /// The dongle's Result codes, as the table in `dongle/README.md` reads them.
    private static func reason(_ code: Dongle.Code, named name: String) -> String {
        switch code {
        case .typed: return "\(name) typed it."
        case .stoppedPartway: return "\(name) stopped partway."
        case .notStarted: return "\(name) isn't plugged in to a Computer, or was stopped before the first key."
        case let .cantType(character): return "a US keyboard can't type “\(character)”."
        case .wrongToken: return "\(name) refused this phone's Token. Remove it and add it again."
        case .busy: return "\(name) is busy. Tap again."
        case .badFrame: return "\(name) couldn't read the Send (error 6)."
        case .tooLong: return "the Phrase is longer than \(Dongle.maxPhrase) bytes."
        case let .unknown(raw): return "\(name) failed (error \(raw))."
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
            let why = reason(failure, named: name)
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
