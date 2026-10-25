import SwiftData
import SwiftUI

/// Adds, renames, reorders and deletes pages. The deck swipes through them in this order.
struct PageEditor: View {
    @Environment(\.modelContext) private var context
    @Environment(\.dismiss) private var dismiss
    @Query(sort: \Page.order) private var pages: [Page]
    @State private var newName = ""
    @State private var deleting: Page?
    @State private var saveError: String?
    @AppStorage(DeckSize.compactKey) private var compactColumns = 0
    @AppStorage(DeckSize.regularKey) private var regularColumns = 0

    var body: some View {
        NavigationStack {
            List {
                Section {
                    ForEach(pages) { page in
                        PageRow(page: page) { name in attempt { try Deck.rename(page, to: name) } }
                            .swipeActions {
                                Button("Delete", role: .destructive) { deleting = page }
                            }
                    }
                    .onMove { from, to in attempt { try Deck.movePages(pages, from: from, to: to) } }
                }
                Section {
                    HStack {
                        TextField("New page", text: $newName)
                            .onSubmit(add)
                            .accessibilityIdentifier("pages.newName")
                        Button("Add", action: add)
                            .disabled(newName.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                }
                Section {
                    Button("Reset button size") {
                        compactColumns = 0
                        regularColumns = 0
                    }
                    .disabled(compactColumns == 0 && regularColumns == 0)
                    .accessibilityIdentifier("pages.resetButtonSize")
                } footer: {
                    Text("Pinch the deck to make buttons bigger or smaller.")
                }
            }
            .navigationTitle("Pages")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) { EditButton() }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
            .confirmationDialog(deleteTitle, isPresented: isDeleting, titleVisibility: .visible) {
                Button("Delete page and its buttons", role: .destructive) {
                    if let deleting { attempt { try Deck.delete(deleting, in: context) } }
                }
            }
            .saveFailureAlert($saveError)
        }
    }

    private func attempt(_ edit: () throws -> Void) {
        do { try edit() } catch { saveError = error.localizedDescription }
    }

    private var isDeleting: Binding<Bool> {
        Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } })
    }

    private var deleteTitle: String {
        let count = deleting?.phrases?.count ?? 0
        return "Delete \"\(deleting?.name ?? "")\" and its \(count) button\(count == 1 ? "" : "s")?"
    }

    private func add() {
        guard !newName.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        attempt {
            try Deck.addPage(named: newName, in: context)
            newName = ""
        }
    }
}

/// One page's name, renamed in place: saved on Return or when the row goes away (Done),
/// not on every keystroke.
private struct PageRow: View {
    let page: Page
    let rename: (String) -> Void
    @State private var name = ""

    var body: some View {
        TextField("Name", text: $name)
            .onAppear { name = page.name }
            .onSubmit { rename(name); name = page.name }
            .onDisappear { rename(name) }
    }
}
