import SwiftUI

/// Adds a computer by tailnet name and pairs it with the code from
/// `phrased pair`, or pairs an existing computer again after a 401.
struct AddHelperView: View {
    var pairAgain: Helper?

    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var host = ""
    @State private var port = ""
    @State private var code = ""
    @State private var error: String?
    @State private var working = false

    private var name: String { pairAgain?.name ?? (host.isEmpty ? "that computer" : host) }

    var body: some View {
        NavigationStack {
            Form {
                if pairAgain == nil {
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
                ToolbarItem(placement: .confirmationAction) {
                    Button("Pair", action: pair)
                        .disabled(working || code.filter(\.isNumber).count != 6 || (pairAgain == nil && host.isEmpty))
                }
            }
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
