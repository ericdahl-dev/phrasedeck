import SwiftUI

/// The computer picker at the top, with the selected computer's status dot.
struct HelperPicker: View {
    let showComputers: () -> Void

    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        Menu {
            Picker("Computer", selection: $model.selectedHelperID) {
                ForEach(model.helpers) { helper in
                    Text(helper.name).tag(Optional(helper.id))
                }
            }
            Divider()
            Button("Computers…", systemImage: "desktopcomputer", action: showComputers)
        } label: {
            HStack(spacing: 6) {
                if let helper = model.selectedHelper {
                    StatusDot(status: model.status(of: helper))
                    Text(helper.name).font(.headline)
                } else {
                    Text("Add a computer").font(.headline)
                }
                Image(systemName: "chevron.down").font(.caption)
            }
        }
        .accessibilityIdentifier("helperPicker")
    }
}

struct StatusDot: View {
    let status: AppModel.Status

    var body: some View {
        Circle()
            .fill(color)
            .frame(width: 10, height: 10)
            .accessibilityLabel(label)
    }

    private var color: Color {
        switch status {
        case .reachable: .green
        case .notReady: .orange
        case .unreachable: .red
        case .unknown: .gray
        }
    }

    private var label: String {
        switch status {
        case .reachable: "reachable"
        case .notReady: "needs Accessibility permission"
        case .unreachable: "unreachable"
        case .unknown: "checking"
        }
    }
}
