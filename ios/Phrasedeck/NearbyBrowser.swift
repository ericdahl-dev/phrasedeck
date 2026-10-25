import Foundation
import Network

/// Browses the home network for `_phrasedeck._tcp` helpers (#11) and
/// resolves each to an address. Only the TXT `ts` name and the resolved
/// address are used: Bonjour is unauthenticated, so the route it enables is
/// always pinned to a fingerprint learned elsewhere.
@MainActor
final class NearbyBrowser {
    private let onChange: @MainActor ([NearbyService]) -> Void
    private var browser: NWBrowser?
    private var results: Set<NWBrowser.Result> = []
    private var addresses: [NWEndpoint: LANAddress] = [:]
    private var resolving: Set<NWEndpoint> = []

    init(onChange: @escaping @MainActor ([NearbyService]) -> Void) {
        self.onChange = onChange
    }

    func start() {
        guard browser == nil else { return }
        let browser = NWBrowser(for: .bonjourWithTXTRecord(type: "_phrasedeck._tcp", domain: nil), using: .tcp)
        browser.browseResultsChangedHandler = { [weak self] results, _ in
            MainActor.assumeIsolated { self?.update(results) }
        }
        browser.start(queue: .main)
        self.browser = browser
    }

    private func update(_ results: Set<NWBrowser.Result>) {
        self.results = results
        let current = Set(results.map(\.endpoint))
        addresses = addresses.filter { current.contains($0.key) }
        publish()
        for endpoint in current where addresses[endpoint] == nil && !resolving.contains(endpoint) {
            resolve(endpoint)
        }
    }

    private func publish() {
        onChange(results.compactMap { result in
            guard case let .bonjour(txt) = result.metadata else { return nil }
            return NearbyService(txt: txt.dictionary, address: addresses[result.endpoint])
        })
    }

    /// Bonjour gives a service, not an address: connect once (IPv4
    /// preferred, since IPv6 link-local zones don't survive URLs) and read
    /// the address the connection reached, then hang up.
    private func resolve(_ endpoint: NWEndpoint) {
        resolving.insert(endpoint)
        let parameters = NWParameters.tcp
        (parameters.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options)?.version = .v4
        let connection = NWConnection(to: endpoint, using: parameters)
        let finish: @MainActor (LANAddress?) -> Void = { [weak self] address in
            connection.cancel()
            guard let self, self.resolving.remove(endpoint) != nil else { return }
            if let address { self.addresses[endpoint] = address; self.publish() }
        }
        connection.stateUpdateHandler = { state in
            MainActor.assumeIsolated {
                switch state {
                case .ready: finish(connection.currentPath?.remoteEndpoint.flatMap(Self.lanAddress))
                case .failed, .waiting: finish(nil)
                default: break
                }
            }
        }
        connection.start(queue: .main)
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { MainActor.assumeIsolated { finish(nil) } }
    }

    /// The endpoint as a home-network address, or nil if it isn't a private one.
    nonisolated static func lanAddress(from endpoint: NWEndpoint) -> LANAddress? {
        guard case let .hostPort(host, port) = endpoint else { return nil }
        let text: String
        switch host {
        case let .ipv4(address): text = "\(address)"
        case let .ipv6(address): text = "\(address)"
        case let .name(name, _): text = name
        @unknown default: return nil
        }
        let unzoned = String(text.prefix { $0 != "%" })
        guard let address = PairLink.privateLANAddress(unzoned) else { return nil }
        return LANAddress(host: address, port: Int(port.rawValue))
    }
}
