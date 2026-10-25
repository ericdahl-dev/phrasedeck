import Foundation
import Security
import Testing
@testable import Phrasedeck

/// Two throwaway self-signed ECDSA certs (openssl, CN=phrased-test and
/// CN=other) and the SHA-256 of the first one's DER.
enum TestCerts {
    static let helper = Data(base64Encoded: "MIIBczCCARmgAwIBAgIUN6Vv+OV+KhzjZpar1/f0W2k0oIIwCgYIKoZIzj0EAwIwFzEVMBMGA1UEAwwMcGhyYXNlZC10ZXN0MB4XDTI2MTAyNTA1MTEyN1oXDTM2MTAyMjA1MTEyN1owFzEVMBMGA1UEAwwMcGhyYXNlZC10ZXN0MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEtKDPbwdUetBlPYR8yeenpChkVsQkm0NU4ZteNAWZezywBv7DaiL+Pf5/7J1jCO1QFj3i6xO/6Wch7sYGeUces6NDMEEwHQYDVR0OBBYEFGPuzf0STT1qm0YpOAI1Qq0JucYfMA8GA1UdEwEB/wQFMAMBAf8wDwYDVR0RBAgwBocEfwAAATAKBggqhkjOPQQDAgNIADBFAiAScQ2UbxvsqJJpORQuJvEF3gn8tNld2hmw6+I3z2gN6wIhAMCQPBCk+FSCeofP9IVWLya1CNVFXPgovt5G/M4O1f1r")!
    static let other = Data(base64Encoded: "MIIBZDCCAQugAwIBAgIUWWqzsKjY7cqu3XmIM4Fod88Yj2wwCgYIKoZIzj0EAwIwEDEOMAwGA1UEAwwFb3RoZXIwHhcNMjYxMDI1MDUxMTI3WhcNMzYxMDIyMDUxMTI3WjAQMQ4wDAYDVQQDDAVvdGhlcjBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABK24daXRQFZxJBgeoqEuVbrghdFCEFKKY9j+npJScTmePFC5HA6XVjzEC2Xwmm2dX1pC1MjDrXhi8xKILBeIs9qjQzBBMB0GA1UdDgQWBBR5uk3vKBZWkJNkr3Gio04KAJ6ZEDAPBgNVHRMBAf8EBTADAQH/MA8GA1UdEQQIMAaHBH8AAAEwCgYIKoZIzj0EAwIDRwAwRAIgcQPLgtl3xUGCWRR9QdUq8oosrsY7ppxt0Zh5Q9KPG/4CIAax5K6VeuH2W8f0f1WAZIk1O1sH3aZpG2cmNRuCcV8j")!
    static let helperPin = "c671d1cd9974f0b6b921b71c9874810da82e1fa914b1131ded17d25890b1c6c1"

    static func trust(_ der: Data) -> SecTrust {
        let cert = SecCertificateCreateWithData(nil, der as CFData)!
        var trust: SecTrust?
        SecTrustCreateWithCertificates(cert, SecPolicyCreateBasicX509(), &trust)
        return trust!
    }
}

struct PinningTests {
    @Test func theLeafMatchesItsPin() {
        #expect(PinningDelegate.leaf(of: TestCerts.trust(TestCerts.helper), matches: TestCerts.helperPin))
    }

    @Test func anotherCertDoesNotMatch() {
        #expect(!PinningDelegate.leaf(of: TestCerts.trust(TestCerts.other), matches: TestCerts.helperPin))
    }

    @Test func theDelegateAcceptsAMatchAndCancelsAMismatch() async {
        let good = PinningDelegate(pin: TestCerts.helperPin)
        let (accept, credential) = good.decide(trust: TestCerts.trust(TestCerts.helper))
        #expect(accept == .useCredential && credential != nil)
        #expect(!good.mismatched)

        let bad = PinningDelegate(pin: TestCerts.helperPin)
        let (refuse, none) = bad.decide(trust: TestCerts.trust(TestCerts.other))
        #expect(refuse == .cancelAuthenticationChallenge && none == nil)
        #expect(bad.mismatched)
    }

    @Test(arguments: [
        ("C6:71:D1:CD:99:74:F0:B6:B9:21:B7:1C:98:74:81:0D:A8:2E:1F:A9:14:B1:13:1D:ED:17:D2:58:90:B1:C6:C1", TestCerts.helperPin),
        (TestCerts.helperPin.uppercased(), TestCerts.helperPin),
        (TestCerts.helperPin, TestCerts.helperPin),
    ])
    func fingerprintsNormalize(raw: String, expected: String) {
        #expect(Fingerprint.normalize(raw) == expected)
    }

    @Test(arguments: ["", "abc", String(repeating: "g", count: 64), String(repeating: "a", count: 63), String(repeating: "a", count: 65)])
    func badFingerprintsAreRejected(raw: String) {
        #expect(Fingerprint.normalize(raw) == nil)
    }

    /// A session reuses TLS connections without asking the delegate again,
    /// so requests for different pins must never share one.
    @Test func eachPinGetsItsOwnSession() {
        let mac = Helper(name: "m", host: "m")
        func session(_ pin: String) -> URLSession {
            HelperClient(helper: mac, token: nil, route: .lan(host: "192.168.1.2", port: 47812, pin: pin)).session
        }
        let other = Fingerprint.of(TestCerts.other)
        #expect(session(TestCerts.helperPin) === session(TestCerts.helperPin))
        #expect(session(TestCerts.helperPin) !== session(other))
        #expect(session(other) !== URLSession.shared)
    }

    @Test func theFixturePinIsTheCertsSHA256() {
        #expect(Fingerprint.of(TestCerts.helper) == TestCerts.helperPin)
    }
}
