import SwiftData
import SwiftUI

/// Adds, renames, reorders and deletes pages. The deck swipes through them in this order.
struct PageEditor: View {
    @Environment(\.modelContext) private var context
    @Environment(\.dismiss) private var dismiss
    @Query(sort: \Page.order) private var pages: [Page]
    @State private var newName = ""
    @State private var deleting: Page?

    var body: some View {
        NavigationStack {
            List {
                Section {
                    ForEach(pages) { page in
                        PageRow(page: page)
                            .swipeActions {
                                Button("Delete", role: .destructive) { deleting = page }
                            }
                    }
                    .onMove { Deck.movePages(pages, from: $0, to: $1) }
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
            }
            .navigationTitle("Pages")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) { EditButton() }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        try? context.save()
                        dismiss()
                    }
                }
            }
            .confirmationDialog(deleteTitle, isPresented: isDeleting, titleVisibility: .visible) {
                Button("Delete page and its buttons", role: .destructive) {
                    if let deleting { Deck.delete(deleting, in: context) }
                    try? context.save()
                }
            }
        }
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
        Deck.addPage(named: newName, in: context)
        try? context.save()
        newName = ""
    }
}

/// One page's name, renamed in place.
private struct PageRow: View {
    let page: Page
    @State private var name = ""

    var body: some View {
        TextField("Name", text: $name)
            .onAppear { name = page.name }
            .onSubmit { Deck.rename(page, to: name); name = page.name }
            .onChange(of: name) { Deck.rename(page, to: name) }
    }
}
