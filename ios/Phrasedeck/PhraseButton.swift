import SwiftUI

struct PhraseButton: View {
    let phrase: Phrase

    @Environment(AppModel.self) private var model
    @State private var flash: AppModel.Feedback.Tone?
    @State private var taps = 0

    var body: some View {
        Button(action: send) {
            ZStack(alignment: .topTrailing) {
                Text(phrase.label)
                    .font(.title3.weight(.semibold))
                    .multilineTextAlignment(.center)
                    .minimumScaleFactor(0.6)
                    .frame(maxWidth: .infinity, minHeight: 96)
                    .padding(8)
                if phrase.pressEnter {
                    Text("⏎")
                        .font(.caption.weight(.bold))
                        .padding(6)
                        .accessibilityLabel("presses Enter")
                }
            }
            .foregroundStyle(.white)
            .background(background, in: .rect(cornerRadius: 16))
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(phrase.seedKey ?? "phrase.\(phrase.label)")
        .sensoryFeedback(.impact(weight: .light), trigger: taps)
    }

    private var background: Color {
        switch flash {
        case .success: .green
        case .amber: .orange
        case .red: .red
        case nil: Color(named: phrase.colorName)
        }
    }

    /// One tap, one send: the result flashes, and nothing is retried.
    private func send() {
        taps += 1
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
