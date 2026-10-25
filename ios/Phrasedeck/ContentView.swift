import SwiftData
import SwiftUI

struct ContentView: View {
    var stubProbe: StubHelper.Probe?

    @Environment(AppModel.self) private var model
    @Environment(\.scenePhase) private var scenePhase
    @State private var showingComputers = false
    @State private var pairing: Helper?
    @State private var incomingLink: IncomingLink?

    /// A phrasedeck://pair link opened from the Camera app or a tap.
    private struct IncomingLink: Identifiable {
        let id = UUID()
        let result: Result<PairLink, PairLink.Invalid>
    }

    var body: some View {
        NavigationStack {
            DeckView()
                .overlay(alignment: .top) { banner }
                .toolbar {
                    ToolbarItem(placement: .principal) {
                        HelperPicker(showComputers: { showingComputers = true })
                    }
                }
                .navigationBarTitleDisplayMode(.inline)
        }
        .sheet(isPresented: $showingComputers) { ComputersView() }
        .sheet(item: $pairing) { helper in AddHelperView(pairAgain: helper) }
        .sheet(item: $incomingLink) { incoming in AddHelperView(link: incoming.result) }
        .onOpenURL { url in
            showingComputers = false
            pairing = nil
            incomingLink = IncomingLink(result: Result { try PairLink(url: url) }
                .mapError { $0 as? PairLink.Invalid ?? .notAPairingLink })
        }
        .task(id: PollKey(helperID: model.selectedHelperID, active: scenePhase == .active)) {
            await pollStatus()
        }
        .overlay(alignment: .bottomTrailing) { probe }
    }

    /// The status dot polls /health every 5 seconds while the app is in
    /// the foreground.
    private func pollStatus() async {
        guard scenePhase == .active, let helper = model.selectedHelper else { return }
        while !Task.isCancelled {
            await model.refreshStatus(of: helper)
            try? await Task.sleep(for: .seconds(5))
        }
    }

    private struct PollKey: Equatable {
        let helperID: UUID?
        let active: Bool
    }

    @ViewBuilder private var banner: some View {
        if let feedback = model.feedback, feedback.tone != .success, let message = feedback.message {
            HStack {
                Text(message)
                    .font(.callout.weight(.semibold))
                    .accessibilityIdentifier("banner.message")
                Spacer()
                if let helper = feedback.pairAgain {
                    Button("Pair again") { pairing = helper }
                        .buttonStyle(.borderedProminent)
                        .tint(.white.opacity(0.25))
                }
            }
            .foregroundStyle(.white)
            .padding()
            .background(feedback.tone == .amber ? Color.orange : Color.red, in: .rect(cornerRadius: 12))
            .padding(.horizontal)
            .onTapGesture { model.dismissFeedback() }
            .transition(.move(edge: .top).combined(with: .opacity))
        }
    }

    @ViewBuilder private var probe: some View {
        if let stubProbe {
            Text("\(stubProbe.sendCount)")
                .font(.caption2)
                .accessibilityIdentifier("stub.sendCount")
                .padding()
        }
    }
}
