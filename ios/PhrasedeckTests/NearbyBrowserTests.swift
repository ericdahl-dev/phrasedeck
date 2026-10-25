import Network
import Testing
@testable import Phrasedeck

struct NearbyBrowserTests {
    @Test(arguments: [("192.168.1.20", 47812), ("10.0.0.7", 47900), ("fd12::5", 47812)])
    func privateAddressesAreKept(host: String, port: Int) {
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(host), port: NWEndpoint.Port(integerLiteral: UInt16(port)))
        #expect(NearbyBrowser.lanAddress(from: endpoint) == LANAddress(host: host, port: port))
    }

    /// A Bonjour answer can point anywhere; only home-network addresses are used.
    @Test(arguments: ["8.8.8.8", "100.69.235.48", "169.254.3.3", "127.0.0.1"])
    func otherAddressesAreDropped(host: String) {
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(host), port: 47812)
        #expect(NearbyBrowser.lanAddress(from: endpoint) == nil)
    }

    @Test func aZonedIPv6AddressLosesItsZone() {
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host("fd12::5%en0"), port: 47812)
        #expect(NearbyBrowser.lanAddress(from: endpoint) == LANAddress(host: "fd12::5", port: 47812))
    }
}
