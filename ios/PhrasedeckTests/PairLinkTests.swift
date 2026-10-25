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

    // MARK: LAN fields (#11)

    static let pin = String(repeating: "ab", count: 32)

    @Test(arguments: ["192.168.1.20", "10.0.0.5", "172.16.0.1", "172.31.255.254", "fd12:3456::1", "fc00::1"])
    func privateLANAddressesWithAFingerprintParse(lan: String) throws {
        let link = try parse("phrasedeck://pair?host=mac.tail1.ts.net&code=123456&v=1&lan=\(lan)&fp=\(Self.pin)").get()
        #expect(link.lan == lan)
        #expect(link.fingerprint == Self.pin)
        #expect(link.host == "mac.tail1.ts.net")
    }

    @Test func aBracketedIPv6LANAddressIsAccepted() throws {
        let link = try parse("phrasedeck://pair?host=mac.tail1.ts.net&code=123456&v=1&lan=%5Bfd12::1%5D&fp=\(Self.pin)").get()
        #expect(link.lan == "fd12::1")
    }

    @Test func aColonFingerprintIsNormalized() throws {
        let colons = stride(from: 0, to: 64, by: 2).map { i in
            let s = Self.pin.uppercased(); let a = s.index(s.startIndex, offsetBy: i)
            return String(s[a..<s.index(a, offsetBy: 2)])
        }.joined(separator: ":")
        let link = try parse("phrasedeck://pair?host=mac.tail1.ts.net&code=123456&v=1&fp=\(colons)").get()
        #expect(link.fingerprint == Self.pin)
        #expect(link.lan == nil)
    }

    @Test func withoutLANFieldsNothingChanges() throws {
        let link = try parse("phrasedeck://pair?host=mac.tail1.ts.net&code=123456&v=1").get()
        #expect(link.lan == nil && link.fingerprint == nil)
    }

    @Test(arguments: [
        ("lan=192.168.1.20", PairLink.Invalid.lanWithoutFingerprint),
        ("lan=8.8.8.8&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("lan=100.69.235.48&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("lan=172.32.0.1&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("lan=2001:db8::1&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("lan=mac.local&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("lan=169.254.10.1&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("lan=fe80::1&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("lan=192.168.1.20&fp=\(PairLinkTests.pin)&lanPort=0", .badPort),
        ("lan=192.168.1.20&fp=\(PairLinkTests.pin)&lanPort=70000", .badPort),
        ("lan=192.168.1.20:47812&fp=\(PairLinkTests.pin)", .badLANAddress),
        ("fp=abc", .badFingerprint),
        ("fp=\(String(repeating: "z", count: 64))", .badFingerprint),
    ])
    func badLANFieldsAreRejected(extra: String, expected: PairLink.Invalid) {
        #expect(throws: expected) {
            try PairLink(url: URL(string: "phrasedeck://pair?host=mac.tail1.ts.net&code=123456&v=1&\(extra)")!)
        }
    }

    /// What #37 prints with Tailscale off: no host or port.
    @Test func aLinkWithoutAHostPairsByLANAddressAndFingerprint() throws {
        let link = try parse("phrasedeck://pair?code=123456&fp=\(Self.pin)&lan=192.168.1.30&lanPort=47900&v=1").get()
        #expect(link.host == nil)
        #expect(link.lan == "192.168.1.30" && link.lanPort == 47900 && link.fingerprint == Self.pin)
        #expect(link.name == "192.168.1.30")
    }

    @Test func lanPortDefaultsTo47812() throws {
        let link = try parse("phrasedeck://pair?code=123456&fp=\(Self.pin)&lan=192.168.1.30&v=1").get()
        #expect(link.lanPort == 47812)
    }

    @Test(arguments: ["phrasedeck://pair?code=123456&fp=\(PairLinkTests.pin)&v=1",
                      "phrasedeck://pair?code=123456&lan=192.168.1.30&v=1",
                      "phrasedeck://pair?code=123456&v=1"])
    func withoutAHostBothLANFieldsAreNeeded(url: String) {
        #expect(throws: (any Error).self) { try PairLink(url: URL(string: url)!) }
    }

    @Test func aBadHostIsRejectedEvenWithLANFields() {
        #expect(throws: PairLink.Invalid.notATailnetHost) {
            try PairLink(url: URL(string: "phrasedeck://pair?host=evil.example.com&code=123456&v=1&lan=192.168.1.20&fp=\(Self.pin)")!)
        }
    }

    @Test func everyRejectionHasAMessage() {
        for invalid in PairLink.Invalid.allCases {
            #expect(!invalid.message.isEmpty)
        }
    }
}
