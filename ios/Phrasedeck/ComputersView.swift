import SwiftUI

/// The computers on this device. Swipe to delete, which also clears the
/// computer's Keychain token.
struct ComputersView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var adding = false

    var body: some View {
        NavigationStack {
            List {
                ForEach(model.helpers) { helper in
                    HStack {
                        StatusDot(status: model.status(of: helper))
                        VStack(alignment: .leading) {
                            Text(helper.name)
                            if helper.port != Helper.defaultPort {
                                Text("port \(helper.port)").font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                .onDelete { offsets in
                    for helper in offsets.map({ model.helpers[$0] }) { model.delete(helper) }
                }
            }
            .overlay {
                if model.helpers.isEmpty {
                    ContentUnavailableView("No computers", systemImage: "desktopcomputer",
                                           description: Text("Add one by its tailnet name."))
                }
            }
            .navigationTitle("Computers")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
                ToolbarItem(placement: .primaryAction) {
                    Button("Add", systemImage: "plus") { adding = true }
                }
            }
            .sheet(isPresented: $adding) { AddHelperView() }
        }
    }
}
