import Foundation
import Testing
@testable import Phrasedeck

/// Records every call and answers from a script, standing in for a helper.
final class FakeHelperAPI: HelperAPI, @unchecked Sendable {
    private let lock = NSLock()
    private var _typed: [(text: String, enter: Bool)] = []
    private var _paired: [(code: String, device: String)] = []
    var typeError: HelperClient.Failure?
    var pairResult: Result<String, HelperClient.Failure> = .success("new-token")
    var healthResult: Result<Health, HelperClient.Failure> =
        .success(Health(hostname: "lib-2833", os: "darwin", accessibilityOK: true))

    var typed: [(text: String, enter: Bool)] { lock.withLock { _typed } }
    var paired: [(code: String, device: String)] { lock.withLock { _paired } }

    func type(text: String, enter: Bool) async throws {
        lock.withLock { _typed.append((text, enter)) }
        if let typeError { throw typeError }
    }

    func pair(code: String, device: String) async throws -> String {
        lock.withLock { _paired.append((code, device)) }
        return try pairResult.get()
    }

    func health() async throws -> Health { try healthResult.get() }
}

@MainActor
@Suite(.serialized)
struct AppModelTests {
    let defaults: UserDefaults
    let tokens = TokenStore(service: "dev.ericdahl.phrasedeck.tests")
    let api = FakeHelperAPI()
    let mac = Helper(name: "lib-2833", host: "lib-2833")
    var clientsMade: [(Helper, String?)] = []

    init() throws {
        let suite = "AppModelTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    func model(withMac: Bool = true) throws -> AppModel {
        let store = HelperStore(defaults: defaults, tokens: tokens)
        if withMac {
            try store.add(mac)
            store.selectedHelperID = mac.id
            try tokens.set("tok", for: mac.id)
        }
        let api = self.api
        return AppModel(store: store, tokens: tokens, device: "Eric's iPhone") { _, _ in api }
    }

    // MARK: sending

    @Test func aTapSendsOnceAndFlashesGreen() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        let feedback = await model.send(text: "yes", enter: true)

        #expect(feedback.tone == .success)
        #expect(api.typed.count == 1)
        #expect(api.typed.first?.text == "yes")
        #expect(api.typed.first?.enter == true)
    }

    @Test(arguments: [
        (HelperClient.Failure.noReply, AppModel.Feedback.Tone.amber, "no reply, it may have typed"),
        (.unreachable, .red, "lib-2833 unreachable"),
        (.unauthorized, .red, "lib-2833 refused this device. Pair again."),
        (.notReady, .red, "lib-2833 can't type yet: grant Accessibility to phrased"),
        (.rateLimited, .red, "Too many sends. Wait a moment."),
        (.server(500), .red, "lib-2833 failed (error 500)"),
    ])
    func failuresMapToTheirFeedbackWithoutRetrying(failure: HelperClient.Failure, tone: AppModel.Feedback.Tone,
                                                   message: String) async throws {
        defer { try? tokens.delete(mac.id) }
        api.typeError = failure
        let model = try model()

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.tone == tone)
        #expect(feedback.message == message)
        #expect(api.typed.count == 1)
        #expect(model.feedback == feedback)
    }

    @Test func unauthorizedOffersPairAgainForThatComputer() async throws {
        defer { try? tokens.delete(mac.id) }
        api.typeError = .unauthorized
        let model = try model()

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.pairAgain == mac)
    }

    @Test func withNoComputerNothingIsSent() async throws {
        let model = try model(withMac: false)

        let feedback = await model.send(text: "x", enter: false)

        #expect(feedback.tone == .red)
        #expect(feedback.message == "Add a computer first.")
        #expect(api.typed.isEmpty)
    }

    // MARK: status dot

    @Test func statusFollowsHealth() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .reachable)

        api.healthResult = .success(Health(hostname: "lib-2833", os: "darwin", accessibilityOK: false))
        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .notReady)

        api.healthResult = .failure(.unreachable)
        await model.refreshStatus(of: mac)
        #expect(model.status(of: mac) == .unreachable)
    }

    // MARK: adding, pairing, deleting

    @Test func addingAComputerPairsSavesTheTokenAndSelectsIt() async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(host: " omarchy-laptop ", port: nil, code: "123456")

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        #expect(added.host == "omarchy-laptop")
        #expect(added.name == "omarchy-laptop")
        #expect(added.port == Helper.defaultPort)
        #expect(model.selectedHelperID == added.id)
        #expect(try tokens.get(added.id) == "new-token")
        #expect(api.paired.first?.code == "123456")
        #expect(api.paired.first?.device == "Eric's iPhone")
    }

    @Test func aMagicDNSNameIsShownByItsShortName() async throws {
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "lib-2833.tail1fdac.ts.net", port: 47900, code: "123456")

        #expect(error == nil)
        let added = try #require(model.helpers.first)
        defer { try? tokens.delete(added.id) }
        #expect(added.name == "lib-2833")
        #expect(added.host == "lib-2833.tail1fdac.ts.net")
        #expect(added.port == 47900)
    }

    @Test func addingAComputerThatIsAlreadyThereRepairsIt() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        let error = await model.addHelper(host: mac.host, port: mac.port, code: "123456")

        #expect(error == nil)
        #expect(model.helpers == [mac])
        #expect(model.selectedHelperID == mac.id)
        #expect(try tokens.get(mac.id) == "new-token")
    }

    @Test func aMissingPairingWindowSaysToRunPhrasedPairFirst() async throws {
        api.pairResult = .failure(.noPairingWindow)
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop", port: 47811, code: "123456")

        #expect(error == "Run phrased pair on omarchy-laptop first.")
        #expect(model.helpers.isEmpty)
    }

    @Test func aWrongCodeSaysSo() async throws {
        api.pairResult = .failure(.unauthorized)
        let model = try model(withMac: false)

        let error = await model.addHelper(host: "omarchy-laptop", port: nil, code: "000000")

        #expect(error == "That code didn't match. Check it, or run phrased pair again.")
        #expect(model.helpers.isEmpty)
    }

    @Test func pairAgainReplacesTheToken() async throws {
        defer { try? tokens.delete(mac.id) }
        let model = try model()

        let error = await model.pair(mac, code: "654321")

        #expect(error == nil)
        #expect(try tokens.get(mac.id) == "new-token")
        #expect(model.helpers == [mac])
    }

    @Test func deletingAComputerClearsItsTokenAndSelection() async throws {
        let model = try model()

        model.delete(mac)

        #expect(model.helpers.isEmpty)
        #expect(model.selectedHelperID == nil)
        #expect(try tokens.get(mac.id) == nil)
    }
}
