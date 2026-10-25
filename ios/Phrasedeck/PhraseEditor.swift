import SwiftData
import SwiftUI

/// Edits one phrase, or adds a new one to a page. Changes apply only on Save.
struct PhraseEditor: View {
    enum Target: Identifiable {
        case new(Page)
        case existing(Phrase)

        var id: PersistentIdentifier {
            switch self {
            case .new(let page): page.persistentModelID
            case .existing(let phrase): phrase.persistentModelID
            }
        }
    }

    let target: Target

    @Environment(\.modelContext) private var context
    @Environment(\.dismiss) private var dismiss
    @Query(sort: \Page.order) private var pages: [Page]
    @State private var draft = PhraseDraft()
    @State private var saveError: String?

    var body: some View {
        NavigationStack {
            Form {
                Section("Button") {
                    TextField("Label", text: $draft.label)
                        .accessibilityIdentifier("editor.label")
                    Picker("Page", selection: $draft.page) {
                        ForEach(pages) { page in
                            Text(page.name).tag(Optional(page))
                        }
                    }
                }
                Section("Text it types") {
                    TextEditor(text: $draft.text)
                        .frame(minHeight: 100)
                        .font(.body.monospaced())
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                        .accessibilityIdentifier("editor.text")
                }
                Section {
                    Toggle("Press Enter after", isOn: $draft.pressEnter)
                    Toggle("Double-tap to send", isOn: $draft.confirm)
                }
                Section {
                    SpanPicker(columns: $draft.spanColumns, rows: $draft.spanRows)
                } header: {
                    Text("Size")
                } footer: {
                    Text("Pinch the deck to change how big one square is.")
                }
                                Section("Color") {
                    HStack {
                        ForEach(PhraseColor.all, id: \.self) { name in
                            Button {
                                draft.colorName = name
                            } label: {
                                Circle()
                                    .fill(Color(named: name))
                                    .frame(width: 34, height: 34)
                                    .overlay {
                                        if draft.colorName == name {
                                            Image(systemName: "checkmark").foregroundStyle(.white).bold()
                                        }
                                    }
                            }
                            .buttonStyle(.plain)
                            .accessibilityLabel(name)
                            .accessibilityAddTraits(draft.colorName == name ? .isSelected : [])
                            .frame(maxWidth: .infinity)
                        }
                    }
                }
            }
            .navigationTitle(isNew ? "New phrase" : "Edit phrase")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save", action: save)
                        .disabled(!draft.isValid)
                        .accessibilityIdentifier("editor.save")
                }
            }
        }
        .onAppear(perform: load)
        .saveFailureAlert($saveError)
    }

    private var isNew: Bool {
        if case .new = target { true } else { false }
    }

    private func load() {
        switch target {
        case .new(let page): draft = PhraseDraft(page: page)
        case .existing(let phrase): draft = PhraseDraft(phrase)
        }
    }

    /// On a failed save the sheet stays open with the draft, so Save can be tried again.
    private func save() {
        do {
            switch target {
            case .new(let page): try Deck.addPhrase(draft, to: draft.page ?? page, in: context)
            case .existing(let phrase): try Deck.save(draft, into: phrase)
            }
            dismiss()
        } catch {
            saveError = error.localizedDescription
        }
    }
}

/// Picks a button's span from 1×1 to 3×3: tap the square where the button should end.
struct SpanPicker: View {
    @Binding var columns: Int
    @Binding var rows: Int

    var body: some View {
        HStack(spacing: 16) {
            Grid(horizontalSpacing: 4, verticalSpacing: 4) {
                ForEach(1...3, id: \.self) { row in
                    GridRow {
                        ForEach(1...3, id: \.self) { column in
                            Button {
                                columns = column
                                rows = row
                            } label: {
                                RoundedRectangle(cornerRadius: 5)
                                    .fill(column <= columns && row <= rows ? Color.accentColor : Color.secondary.opacity(0.2))
                                    .frame(width: 34, height: 34)
                            }
                            .buttonStyle(.plain)
                            .accessibilityLabel("\(column) by \(row)")
                            .accessibilityAddTraits(column == columns && row == rows ? .isSelected : [])
                            .accessibilityIdentifier("editor.span.\(column)x\(row)")
                        }
                    }
                }
            }
            Text("\(columns) × \(rows)")
                .font(.title3.monospacedDigit())
                .foregroundStyle(.secondary)
                .accessibilityIdentifier("editor.span.value")
        }
        .padding(.vertical, 4)
    }
}
