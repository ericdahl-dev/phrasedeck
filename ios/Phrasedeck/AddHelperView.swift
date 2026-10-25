import SwiftUI

/// Adds a computer by scanning the QR code from `phrased pair`, by opening
/// its phrasedeck://pair link, or by typing its tailnet name and code; or
/// pairs an existing computer again after a 401.
struct AddHelperView: View {
    var pairAgain: Helper?
    /// A phrasedeck://pair link that was opened, already checked.
    var link: Result<PairLink, PairLink.Invalid>?

    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var host = ""
    @State private var port = ""
    @State private var code = ""
    @State private var error: String?
    @State private var working = false
    @State private var confirming: PairLink?
    @State private var scanning = false

    private var name: String { pairAgain?.name ?? (host.isEmpty ? "that computer" : host) }

    var body: some View {
        NavigationStack {
            Form {
                if let confirming {
                    Section {
                        let shown = confirming.host ?? "\(confirming.lan ?? "") on the home network"
                        Text(shown)
                            .font(.callout.monospaced())
                            .accessibilityIdentifier(confirming.host ?? confirming.lan ?? "")
                        Button("Pair with \(confirming.name)") { pair(confirming) }
                            .font(.headline)
                            .disabled(working)
                            .accessibilityIdentifier("addHelper.confirmLink")
                    } header: {
                        Text("From the QR code")
                    } footer: {
                        Text("Only pair with a computer you just ran phrased pair on.")
                    }
                } else {
                    manualEntry
                }
                if let error {
                    Section {
                        Text(error).foregroundStyle(.red).accessibilityIdentifier("addHelper.error")
                    }
                }
            }
            .navigationTitle(pairAgain == nil ? "Add a computer" : "Pair \(name) again")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                if confirming == nil {
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Pair", action: pair)
                            .disabled(working || code.filter(\.isNumber).count != 6 || (pairAgain == nil && host.isEmpty))
                    }
                }
            }
            .sheet(isPresented: $scanning) {
                QRScannerView { text in
                    scanning = false
                    accept(Result { try PairLink(url: URL(string: text) ?? URL(fileURLWithPath: "/")) }
                        .mapError { $0 as? PairLink.Invalid ?? .notAPairingLink })
                }
                .ignoresSafeArea()
            }
            .onAppear { if let link { accept(link) } }
        }
    }

    @ViewBuilder private var manualEntry: some View {
                if pairAgain == nil {
                    Section {
                        Button("Scan QR code", systemImage: "qrcode.viewfinder") { error = nil; scanning = true }
                            .font(.headline)
                            .disabled(!QRScannerView.isAvailable)
                    } footer: {
                        Text(QRScannerView.isAvailable
                             ? "On the computer, run `phrased pair` and scan the code it shows."
                             : "Scanning needs the camera. Allow it in Settings, or type the name and code below.")
                    }
                    Section {
                        TextField("Tailnet name, e.g. omarchy-laptop", text: $host)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .accessibilityIdentifier("addHelper.host")
                        TextField("Port (default \(Helper.defaultPort))", text: $port)
                            .keyboardType(.numberPad)
                    } header: {
                        Text("Computer")
                    }
                }
                Section {
                    TextField("6-digit code", text: $code)
                        .keyboardType(.numberPad)
                        .textContentType(.oneTimeCode)
                        .font(.title2.monospacedDigit())
                        .accessibilityIdentifier("addHelper.code")
                } header: {
                    Text("Pairing code")
                } footer: {
                    Text("On \(name), run `phrased pair` and enter the code it prints. It's good for 2 minutes.")
                }
    }

    /// A checked link waits for one confirm tap; a bad one never contacts anything.
    private func accept(_ link: Result<PairLink, PairLink.Invalid>) {
        switch link {
        case let .success(link): confirming = link; error = nil
        case let .failure(invalid): confirming = nil; error = invalid.message
        }
    }

    private func pair(_ link: PairLink) {
        working = true
        Task {
            error = await model.addHelper(from: link)
            working = false
            if error == nil { dismiss() } else { confirming = nil }
        }
    }

    private func pair() {
        working = true
        Task {
            if let pairAgain {
                error = await model.pair(pairAgain, code: code)
            } else {
                error = await model.addHelper(host: host, port: Int(port), code: code)
            }
            working = false
            if error == nil { dismiss() }
        }
    }
}
