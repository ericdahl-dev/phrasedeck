import Foundation
import Testing
@testable import Phrasedeck

/// contract/pairing.json, which the helper's Go tests read too: the QR codes
/// `phrased pair` builds, the /pair replies it sends and the private ranges.
struct PairingContract: Decodable {
    struct Offer: Decodable {
        let name: String
        let host: String?
        let port: Int?
        let code: String
        let fp: String?
        let lan: String?
        let lanPort: Int?
        let qr: String
    }
    struct InvalidQR: Decodable { let qr: String; let error: String }
    struct Reply: Decodable {
        let name: String
        let certSHA256: String?
        let lanPort: Int?
        let body: [String: JSONValue]
    }
    struct LANAddresses: Decodable { let `private`: [String]; let notPrivate: [String] }
    enum JSONValue: Codable { case string(String), int(Int)
        init(from decoder: Decoder) throws {
            let c = try decoder.singleValueContainer()
            if let i = try? c.decode(Int.self) { self = .int(i) } else { self = .string(try c.decode(String.self)) }
        }
        func encode(to encoder: Encoder) throws {
            var c = encoder.singleValueContainer()
            switch self { case .string(let s): try c.encode(s); case .int(let i): try c.encode(i) }
        }
    }

    let offers: [Offer]
    let invalidQR: [InvalidQR]
    let replies: [Reply]
    let lanAddresses: LANAddresses

    static let shared: PairingContract = {
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().appending(path: "../../contract/pairing.json")
        return try! JSONDecoder().decode(PairingContract.self, from: Data(contentsOf: url))
    }()
}

struct PairingContractTests {
    @Test(arguments: PairingContract.shared.offers.map { $0.name })
    func theAppParsesEachOfferAsTheHelperBuiltIt(name: String) throws {
        let o = try #require(PairingContract.shared.offers.first { $0.name == name })
        let link = try PairLink(url: try #require(URL(string: o.qr)))
        #expect(link == PairLink(host: o.host, port: o.port ?? Helper.defaultPort, code: o.code, lan: o.lan,
                                 fingerprint: o.fp, lanPort: o.lanPort ?? PairLink.defaultLANPort))
    }

    @Test(arguments: PairingContract.shared.invalidQR.map { $0.qr })
    func theAppRefusesEachBadLink(qr: String) throws {
        let want = try #require(PairingContract.shared.invalidQR.first { $0.qr == qr }).error
        #expect(throws: (any Error).self) { try PairLink(url: URL(string: qr)!) }
        do { _ = try PairLink(url: URL(string: qr)!) } catch { #expect("\(error)" == want) }
    }

    @Test func privateRangesMatchTheHelper() {
        for a in PairingContract.shared.lanAddresses.private { #expect(PairLink.privateLANAddress(a) != nil, "\(a)") }
        for a in PairingContract.shared.lanAddresses.notPrivate { #expect(PairLink.privateLANAddress(a) == nil, "\(a)") }
    }
}

/// In HelperClientTests' serialized suite, since StubURLProtocol is shared.
extension HelperClientTests {
    @Test(arguments: PairingContract.shared.replies.map { $0.name })
    func eachContractPairReplyDecodes(name: String) async throws {
        let r = try #require(PairingContract.shared.replies.first { $0.name == name })
        var body = r.body
        body["token"] = .string("t")
        StubURLProtocol.reset(.status(200, body: try JSONEncoder().encode(body)))

        let pairing = try await client(token: nil).pair(code: "123456", device: "iPhone")

        #expect(pairing == Pairing(token: "t", certSHA256: r.certSHA256, lanPort: r.lanPort))
    }
}
