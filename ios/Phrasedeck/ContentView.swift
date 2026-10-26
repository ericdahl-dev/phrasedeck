import SwiftData
import SwiftUI

struct ContentView: View {
    var stubProbe: StubHelper.Probe?

    @Environment(AppModel.self) private var model
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.accessibilityVoiceOverEnabled) private var voiceOver
    @State private var showingComputers = false
    @State private var addingComputer = false
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
                // An inset, not an overlay, so the banner pushes the grid down instead of covering row 1.
                .safeAreaInset(edge: .top) { banner }
                .toolbar {
                    ToolbarItem(placement: .principal) {
                        HelperPicker(showComputers: { showingComputers = true }, addComputer: { addingComputer = true })
                    }
                }
                .navigationBarTitleDisplayMode(.inline)
        }
        .sheet(isPresented: $showingComputers) { ComputersView() }
        .sheet(isPresented: $addingComputer) { AddHelperView() }
        .sheet(item: $pairing) { helper in AddHelperView(pairAgain: helper) }
        .sheet(item: $incomingLink) { incoming in AddHelperView(link: incoming.result) }
        .onOpenURL { url in
            showingComputers = false
            pairing = nil
            incomingLink = IncomingLink(result: PairLink.parse(url.absoluteString))
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
        // A success only has a message when it took another route (#11).
        if let feedback = model.feedback, let message = feedback.message {
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
                if feedback.addComputer {
                    Button("Add") {
                        model.dismissFeedback()
                        addingComputer = true
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.white.opacity(0.25))
                    .accessibilityIdentifier("banner.add")
                }
                Button { model.dismissFeedback() } label: {
                    Image(systemName: "xmark")
                        .font(.callout.weight(.bold))
                        .frame(width: 44, height: 44)
                        .contentShape(.rect)
                }
                .accessibilityLabel("Close")
            }
            .foregroundStyle(.white)
            .padding()
            .background(feedback.tone == .success ? Color.blue : feedback.tone == .amber ? Color.orange : Color.red,
                        in: .rect(cornerRadius: 12))
            .padding(.horizontal)
            .onTapGesture { model.dismissFeedback() }
            .transition(.move(edge: .top).combined(with: .opacity))
            // VoiceOver users close it themselves, so it can't vanish mid-read.
            .task(id: model.feedbackShown) {
                guard feedback.autoCloses, !voiceOver else { return }
                try? await Task.sleep(for: .seconds(4))
                if !Task.isCancelled { withAnimation { model.dismissFeedback() } }
            }
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
