import Testing
@testable import Phrasedeck

/// Every failure's wording for a send and for pairing, from the one module that owns it (#44).
@MainActor
struct OutcomeTests {
    let mac = Helper(name: "lib-2833", host: "lib-2833")

    @Test(arguments: [
        (HelperClient.Failure.noReply, AppModel.Feedback.Tone.amber, "no reply, it may have typed"),
        // Agreed with the helper: 409 means nothing was typed, so it's safe to tap again.
        (.busy, .red, "lib-2833 is busy, nothing was typed. Tap again."),
        (.failedPartway, .amber, "lib-2833 failed partway, it may have typed"),
        (.unreachable, .red, "lib-2833 unreachable"),
        (.unauthorized, .red, "lib-2833 refused this device. Pair again."),
        (.notReady, .red, "lib-2833 can't type yet: grant Accessibility to phrased"),
        (.rateLimited, .red, "Too many sends. Wait a moment."),
        (.notFound, .red, "lib-2833 failed (error 404)"),
        (.server(415), .red, "lib-2833 failed (error 415)"),
        (.notTailnet, .red, "lib-2833 isn't a tailnet name, so nothing was sent. Pair it again by scanning its QR code."),
        (.notSent(-1022), .red, "lib-2833: the connection was blocked before anything was sent (error -1022)."),
        (.pinMismatch, .red, "lib-2833's certificate didn't match, so nothing was sent. Scan its QR code to pair again."),
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
}
