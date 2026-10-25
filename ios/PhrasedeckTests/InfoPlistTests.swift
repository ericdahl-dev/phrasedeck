import Foundation
import Testing

// Runs in the app (test host), so Bundle.main is the app's own Info.plist.
struct InfoPlistTests {
    let info = Bundle.main.infoDictionary ?? [:]

    @Test func allowsPlainHTTPToTailnetNames() throws {
        let ats = try #require(info["NSAppTransportSecurity"] as? [String: Any])
        #expect(ats["NSAllowsLocalNetworking"] as? Bool == true)
        let domains = try #require(ats["NSExceptionDomains"] as? [String: Any])
        let tsNet = try #require(domains["ts.net"] as? [String: Any])
        #expect(tsNet["NSIncludesSubdomains"] as? Bool == true)
        #expect(tsNet["NSExceptionAllowsInsecureHTTPLoads"] as? Bool == true)
        #expect(ats["NSAllowsArbitraryLoads"] == nil)
    }

    @Test func answersExportComplianceUpFront() {
        #expect(info["ITSAppUsesNonExemptEncryption"] as? Bool == false)
    }

    /// Browsing for helpers on the home network (#11); advertising nothing.
    @Test func browsesOnlyForPhrasedeckHelpers() throws {
        #expect(info["NSBonjourServices"] as? [String] == ["_phrasedeck._tcp"])
        let usage = try #require(info["NSLocalNetworkUsageDescription"] as? String)
        #expect(usage.contains("home network"))
    }

    @Test func opensPhrasedeckLinks() throws {
        let types = try #require(info["CFBundleURLTypes"] as? [[String: Any]])
        let schemes = types.flatMap { $0["CFBundleURLSchemes"] as? [String] ?? [] }
        #expect(schemes == ["phrasedeck"])
    }

    @Test func explainsTheCamera() throws {
        let usage = try #require(info["NSCameraUsageDescription"] as? String)
        #expect(usage.contains("QR"))
    }
}
