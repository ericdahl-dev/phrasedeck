import Testing
@testable import Phrasedeck

/// Every failure's wording for a send and for pairing, from the one module that owns it (#44).
@MainActor
struct OutcomeTests {
    let mac = Helper(name: "lib-2833", host: "lib-2833")

    /// Each message leads with what happened to the text, then why (UX review).
    @Test(arguments: [
        (HelperClient.Failure.noReply, AppModel.Feedback.Tone.amber, "Maybe typed: no reply from lib-2833."),
        // Agreed with the helper: 409 means nothing was typed, so it's safe to tap again.
        (.busy, .red, "Not typed: lib-2833 is busy. Tap again."),
        (.failedPartway, .amber, "Maybe typed: lib-2833 failed partway."),
        (.unreachable, .red, "Not typed: lib-2833 unreachable"),
        (.unauthorized, .red, "Not typed: lib-2833 refused this device. Pair again."),
        (.notReady, .red, "Not typed: lib-2833 can't type yet: grant Accessibility to phrased"),
        (.rateLimited, .red, "Not typed: too many sends. Wait a moment."),
        (.notFound, .red, "Not typed: lib-2833 failed (error 404)"),
        (.server(415), .red, "Not typed: lib-2833 failed (error 415)"),
        (.notTailnet, .red, "Not typed: lib-2833 isn't a tailnet name. Pair it again by scanning its QR code."),
        (.notSent(-1022), .red, "Not typed: the connection to lib-2833 was blocked (error -1022)."),
        (.pinMismatch, .red, "Not typed: lib-2833's certificate didn't match. Scan its QR code to pair again."),
    ])
    func eachSendFailureHasItsWording(failure: HelperClient.Failure, tone: AppModel.Feedback.Tone, message: String) {
        let feedback = Outcome.send(failure, on: mac)
        #expect(feedback.tone == tone)
        #expect(feedback.message == message)
        #expect(feedback.pairAgain == (failure == .unauthorized ? mac : nil))
    }

    @Test(arguments: [
        (HelperClient.Failure.notFound, "Run phrased pair on lib-2833 first."),
        (.unauthorized, "That code didn't match. Check it, or run phrased pair again."),
        (.noReply, "lib-2833 didn't answer. Run phrased pair again and retry."),
        // A /pair 500 (say, the token couldn't be saved) types nothing, so no "may have typed".
        (.failedPartway, "Pairing with lib-2833 failed. Run phrased pair again."),
        (.rateLimited, "Too many pairing attempts. Run phrased pair on lib-2833 again."),
        // Anything else reads as it would for a send.
        (.unreachable, "lib-2833 unreachable"),
        (.notReady, "lib-2833 can't type yet: grant Accessibility to phrased"),
    ])
    func eachPairFailureHasItsWording(failure: HelperClient.Failure, message: String) {
        #expect(Outcome.pair(failure, on: mac) == message)
    }

    /// What the Button shows for a moment after a send, and VoiceOver says.
    @Test func eachToneHasAWordAndASymbol() {
        #expect(AppModel.Feedback.Tone.success.word == "Typed")
        #expect(AppModel.Feedback.Tone.red.word == "Not typed")
        #expect(AppModel.Feedback.Tone.amber.word == "Maybe typed")
        #expect(AppModel.Feedback.Tone.success.symbol == "checkmark")
        #expect(AppModel.Feedback.Tone.red.symbol == "xmark")
        #expect(AppModel.Feedback.Tone.amber.symbol == "questionmark")
    }
}
