import SwiftUI

struct PhraseButton: View {
    let phrase: Phrase
    var metrics = DeckSize.metrics(columnWidth: 112)
    var pinchGuard: PinchGuard?
    var edit: () -> Void = {}
    var delete: () -> Void = {}

    @Environment(AppModel.self) private var model
    @State private var flash: AppModel.Feedback.Tone?
    @State private var taps = 0
    @State private var gate = ConfirmGate()
    @State private var armed = false

    var body: some View {
        Button(action: tapped) {
            ZStack(alignment: .topTrailing) {
                Text(armed ? "tap again" : phrase.label)
                    .font(.system(size: metrics.font, weight: .semibold))
                    .multilineTextAlignment(.center)
                    .lineLimit(3)
                    .minimumScaleFactor(0.5)
                    .frame(maxWidth: .infinity, minHeight: metrics.height - 16, maxHeight: .infinity)
                    .padding(8)
                if phrase.pressEnter {
                    Text("⏎")
                        .font(.system(size: metrics.font * 0.6, weight: .bold))
                        .padding(6)
                        .accessibilityLabel("presses Enter")
                }
            }
            .foregroundStyle(.white)
            .background(background, in: .rect(cornerRadius: 16))
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button("Edit", systemImage: "pencil", action: edit)
            Button("Delete", systemImage: "trash", role: .destructive, action: delete)
        }
        .accessibilityIdentifier(phrase.seedKey ?? "phrase.\(phrase.label)")
        .sensoryFeedback(.impact(weight: .light), trigger: taps)
    }

    private var background: Color {
        switch flash {
        case .success: .green
        case .amber: .orange
        case .red: .red
        case nil: armed ? Color(named: phrase.colorName).opacity(0.6) : Color(named: phrase.colorName)
        }
    }

    /// Phrases marked confirm need a second tap within 1.5 seconds (#8).
    private func tapped() {
        if pinchGuard?.blocksTaps == true { return }
        taps += 1
        guard phrase.confirm else { return send() }
        if gate.tap() {
            armed = false
            send()
        } else {
            armed = true
            Task {
                try? await Task.sleep(for: .seconds(ConfirmGate.window))
                if !gate.isArmed() { armed = false }
            }
        }
    }

    /// One send per call: the result flashes, and nothing is retried.
    private func send() {
        Task {
            let feedback = await model.send(text: phrase.text, enter: phrase.pressEnter)
            withAnimation(.easeOut(duration: 0.15)) { flash = feedback.tone }
            try? await Task.sleep(for: .milliseconds(700))
            withAnimation { flash = nil }
        }
    }
}

extension Color {
    init(named name: String) {
        switch name {
        case "green": self = .green
        case "purple": self = .purple
        case "orange": self = .orange
        case "red": self = .red
        case "gray": self = .gray
        default: self = .blue
        }
    }
}
