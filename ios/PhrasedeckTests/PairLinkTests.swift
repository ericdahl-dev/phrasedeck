import Foundation
import Testing
@testable import Phrasedeck

struct PairLinkTests {
    func parse(_ string: String) -> Result<PairLink, PairLink.Invalid> {
        Result { try PairLink(url: URL(string: string)!) }.mapError { $0 as! PairLink.Invalid }
    }

    @Test func aGoodPayloadParses() throws {
        let link = try parse("phrasedeck://pair?code=012345&host=lib-2833.tail1fdac.ts.net&port=47900&v=1").get()

        #expect(link == PairLink(host: "lib-2833.tail1fdac.ts.net", port: 47900, code: "012345"))
        #expect(link.name == "lib-2833")
    }

    @Test(arguments: ["100.64.0.1", "100.69.235.48", "100.127.255.254", "fd7a:115c:a1e0::1", "FD7A:115C:A1E0:AB12::9"])
    func tailscaleAddressesAreAccepted(host: String) throws {
        let link = try parse("phrasedeck://pair?host=\(host)&port=47811&code=123456&v=1").get()
        #expect(link.host == host.lowercased())
        #expect(link.name == host.lowercased())
    }

    @Test func theNameIsLowercasedAndAMissingPortIsTheDefault() throws {
        let link = try parse("phrasedeck://pair?host=LIB-2833.Tail1fdac.TS.NET.&code=123456&v=1").get()
        #expect(link.host == "lib-2833.tail1fdac.ts.net")
        #expect(link.port == Helper.defaultPort)
    }

    @Test(arguments: [
        ("https://pair?host=a.tail1.ts.net&code=123456&v=1", PairLink.Invalid.notAPairingLink),
        ("phrasedeck://type?host=a.tail1.ts.net&code=123456&v=1", .notAPairingLink),
        ("phrasedeck://pair?host=a.tail1.ts.net&code=123456&v=2", .unsupportedVersion),
        ("phrasedeck://pair?host=a.tail1.ts.net&code=123456", .unsupportedVersion),
        ("phrasedeck://pair?host=evil.example.com&code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?host=ts.net&code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?host=a_b.tail1.ts.net&code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?host=evil.com%2F.ts.net&code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?host=192.168.1.10&code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?host=100.128.0.1&code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?host=fd7a:115c:a1e1::1&code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?code=123456&v=1", .notATailnetHost),
        ("phrasedeck://pair?host=a.tail1.ts.net&code=12345&v=1", .badCode),
        ("phrasedeck://pair?host=a.tail1.ts.net&code=12345a&v=1", .badCode),
        ("phrasedeck://pair?host=a.tail1.ts.net&code=١٢٣٤٥٦&v=1", .badCode),
        ("phrasedeck://pair?host=a.tail1.ts.net&code=123456&port=0&v=1", .badPort),
        ("phrasedeck://pair?host=a.tail1.ts.net&code=123456&port=70000&v=1", .badPort),
    ])
    func badPayloadsAreRejected(url: String, expected: PairLink.Invalid) {
        #expect(throws: expected) { try PairLink(url: URL(string: url)!) }
    }

    /// The Camera app's link and the in-app scanner's text go through the same step.
    @Test func scannedTextParsesOrSaysWhy() throws {
        #expect(try PairLink.parse("phrasedeck://pair?host=lib-2833.tail1fdac.ts.net&port=47811&code=123456&v=1").get().code == "123456")
        #expect(throws: PairLink.Invalid.notAPairingLink) { try PairLink.parse("not a link").get() }
        #expect(throws: PairLink.Invalid.notAPairingLink) { try PairLink.parse("").get() }
    }

    @Test func everyRejectionHasAMessage() {
        for invalid in PairLink.Invalid.allCases {
            #expect(!invalid.message.isEmpty)
        }
    }
}
