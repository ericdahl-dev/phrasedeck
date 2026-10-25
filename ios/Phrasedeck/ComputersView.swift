import SwiftUI

/// The computers on this device. Swipe to delete, which also clears the
/// computer's Keychain token.
struct ComputersView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var adding = false
    @State private var deleteError: String?

    var body: some View {
        NavigationStack {
            List {
                ForEach(model.helpers) { helper in
                    HStack {
                        StatusDot(status: model.status(of: helper))
                        VStack(alignment: .leading) {
                            HStack {
                                Text(helper.name)
                                RouteLabel(route: model.statusRoute(of: helper))
                            }
                            if helper.port != Helper.defaultPort {
                                Text("port \(helper.port)").font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                .onDelete { offsets in
                    // Stops at the first failure, which is shown; that computer stays.
                    for helper in offsets.map({ model.helpers[$0] }) {
                        if let error = model.delete(helper) { deleteError = error; break }
                    }
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
            .alert("Couldn't remove the computer",
                   isPresented: Binding(get: { deleteError != nil }, set: { if !$0 { deleteError = nil } })) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(deleteError ?? "")
            }
        }
    }
}
