import Foundation
import Testing
@testable import Phrasedeck

/// Sending a Phrase and checking a Computer, on every Route, through the Courier (#206), with
/// fakes in place of the Helper and of Bluetooth.
@MainActor
@Suite(.serialized)
struct CourierTests {
    let defaults: UserDefaults
    let tokens = MemoryTokens()
    /// The tailnet route's Helper; `lan` answers on the Home network route.
    let api = FakeHelperAPI()
    let lan = FakeHelperAPI()
    let routes = RouteLog()
    let link = FakeDongleLink()
    let mac = Helper(name: "lib-2833", host: "lib-2833")

    init() throws {
        let suite = "CourierTests.\(UUID().uuidString)"
        defaults = try #require(UserDefaults(suiteName: suite))
        defaults.removePersistentDomain(forName: suite)
    }

    /// Records each connection's route; the tailnet answers with `api`, the LAN with `lan`.
    func makeComputers() -> Computers {
        let (api, lan, routes) = (self.api, self.lan, self.routes)
        return Computers(defaults: defaults, tokens: tokens,
                         dongles: DongleSender(link: link, resultTimeout: { _ in .milliseconds(300) })) { _, _, route in
            routes.log.append(route)
            return route == .tailnet ? api : lan
        }
    }

    /// A Courier for this suite's Computers, with the Mac added and its Token stored.
    func courier() throws -> (Courier, Computers) {
        let computers = makeComputers()
        try computers.add(mac)
        try tokens.set("tok", for: mac.id)
        return (Courier(computers: computers), computers)
    }

    // MARK: the Tailnet route

    @Test func aSendOverTheTailnetIsTyped() async throws {
        let (courier, _) = try courier()

        let delivery = await courier.send("yes", enter: true, to: mac)

        #expect(delivery == .typed(nil))
        #expect(api.typed.count == 1)
        #expect(api.typed.first?.text == "yes")
        #expect(api.typed.first?.enter == true)
        #expect(routes.log == [.tailnet])
    }
}
