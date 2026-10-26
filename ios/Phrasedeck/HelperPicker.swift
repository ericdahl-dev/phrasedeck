import SwiftUI

/// The computer picker at the top, with the selected computer's status dot.
struct HelperPicker: View {
    let showComputers: () -> Void
    let addComputer: () -> Void

    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        if model.helpers.isEmpty {
            // Nothing to pick yet, so skip the menu and go straight to adding one.
            Button(action: addComputer) {
                HStack(spacing: 6) {
                    Image(systemName: "plus.circle.fill")
                    Text("Add a computer").font(.headline)
                }
            }
            .accessibilityIdentifier("helperPicker")
        } else {
            picker
        }
    }

    private var picker: some View {
        @Bindable var model = model
        return Menu {
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
                    RouteLabel(route: model.statusRoute(of: helper))
                } else {
                    Text("Add a computer").font(.headline)
                }
                Image(systemName: "chevron.down").font(.caption)
            }
        }
        .accessibilityIdentifier("helperPicker")
    }
}

/// "home" or "tailnet": the way the last status check reached the computer (#11).
struct RouteLabel: View {
    let route: AppModel.RouteKind?

    var body: some View {
        if let route {
            Text(route == .home ? "home" : "tailnet")
                .font(.caption2.weight(.semibold))
                .foregroundStyle(.secondary)
                .padding(.horizontal, 5)
                .padding(.vertical, 1)
                .background(.quaternary, in: .capsule)
                .accessibilityLabel(route == .home ? "over the home network" : "over the tailnet")
        }
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
