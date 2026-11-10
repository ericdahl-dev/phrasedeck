import SwiftUI

/// Adds a Computer reached through a Phrasedeck USB dongle (#202): the scan lists the dongles
/// nearby, and tapping one bonds with it (iOS asks for the passkey) and keeps its Token.
struct AddDongleView: View {
    /// Closes the whole Add sheet once the dongle is added.
    let onAdded: () -> Void

    @Environment(AppModel.self) private var model
    @State private var found: [FoundDongle] = []
    @State private var adding: UUID?
    @State private var error: String?

    var body: some View {
        Form {
            Section {
                if found.isEmpty, error == nil {
                    HStack(spacing: 8) {
                        ProgressView()
                        Text("Looking for dongles…").foregroundStyle(.secondary)
                    }
                }
                ForEach(found) { dongle in
                    Button { add(dongle) } label: {
                        HStack {
                            Label(dongle.name, systemImage: "cable.connector")
                            Spacer()
                            if adding == dongle.id { ProgressView() }
                        }
                    }
                    .disabled(adding != nil)
                    .accessibilityIdentifier("addDongle.found")
                }
            } header: {
                Text("Nearby")
            } footer: {
                Text("Plug the dongle into the computer it should type on. When iOS asks for a passkey, enter the 6 digits the dongle prints on its USB serial port.")
            }
            if let error {
                Section {
                    Text(error).foregroundStyle(.red).accessibilityIdentifier("addDongle.error")
                }
            }
        }
        .navigationTitle("USB dongle")
        .navigationBarTitleDisplayMode(.inline)
        .task {
            do {
                for try await list in model.scanForDongles() { found = list }
            } catch let failure as Dongle.Failure {
                error = Outcome.pairDongle(failure, named: Dongle.name)
            } catch {
                self.error = error.localizedDescription
            }
        }
    }

    private func add(_ dongle: FoundDongle) {
        adding = dongle.id
        error = nil
        Task {
            error = await model.addDongle(dongle)
            adding = nil
            if error == nil { onAdded() }
        }
    }
}
