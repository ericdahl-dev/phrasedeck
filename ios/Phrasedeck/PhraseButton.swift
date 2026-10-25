import SwiftUI

struct PhraseButton: View {
    let phrase: Phrase
    var metrics = DeckSize.metrics(columnWidth: 112)
    var pinchGuard: PinchGuard?
    var edit: () -> Void = {}
    var delete: () -> Void = {}
    /// Shown in the long-press menu when set (#81).
    var editDeck: (() -> Void)?
    /// Set in Arrange mode: taps don't type, and the move actions replace the menu.
    var arrange: ArrangeRole?

    /// What a Button needs in Arrange mode (#81, spec section 11).
    struct ArrangeRole {
        /// Where it's shown, for VoiceOver's "Row r, column c".
        let spot: Spot
        let nudges: [Arrangement.Nudge]
        /// The other Pages, by name, for Move to Page.
        let otherPages: [String]
        let nudge: (Arrangement.Nudge) -> Void
        let moveToPage: (String) -> Void
    }

    @Environment(AppModel.self) private var model
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
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
            if arrange == nil {
                Button("Edit", systemImage: "pencil", action: edit)
                if let editDeck { Button("Edit Deck", systemImage: "square.grid.3x3", action: editDeck) }
                Button("Delete", systemImage: "trash", role: .destructive, action: delete)
            }
        }
        .modifier(Jiggle(active: arrange != nil && !reduceMotion))
        .overlay {
            // Reduce Motion: a dashed outline instead of the jiggle.
            if arrange != nil && reduceMotion {
                RoundedRectangle(cornerRadius: 16).strokeBorder(style: StrokeStyle(lineWidth: 2, dash: [6, 4]))
                    .foregroundStyle(.secondary)
            }
        }
        .accessibilityIdentifier(phrase.seedKey ?? "phrase.\(phrase.label)")
        .accessibilityValue(arrange.map { "Row \($0.spot.row + 1), column \($0.spot.column + 1)" } ?? "")
        .accessibilityActions {
            if let arrange {
                ForEach(arrange.nudges, id: \.self) { nudge in
                    Button(nudge.actionName) { arrange.nudge(nudge) }
                }
                ForEach(arrange.otherPages, id: \.self) { page in
                    Button("Move to \(page)") { arrange.moveToPage(page) }
                }
            }
        }
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
        // Taps never type in Arrange mode (spec section 3).
        if pinchGuard?.blocksTaps == true || arrange != nil { return }
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

extension Arrangement.Nudge {
    var actionName: String {
        switch self {
        case .left: "Move left"
        case .right: "Move right"
        case .up: "Move up"
        case .down: "Move down"
        }
    }
}

/// The Arrange mode jiggle: about 1.5° each way, about 0.13 s a swing, each Button starting at its
/// own point so they don't move in step (spec section 10).
private struct Jiggle: ViewModifier {
    let active: Bool
    @State private var phase = Double.random(in: 0..<1)

    func body(content: Content) -> some View {
        if active {
            TimelineView(.animation) { timeline in
                let t = timeline.date.timeIntervalSinceReferenceDate / 0.26 + phase
                content.rotationEffect(.degrees(1.5 * sin(t * 2 * .pi)))
            }
        } else {
            content
        }
    }
}
