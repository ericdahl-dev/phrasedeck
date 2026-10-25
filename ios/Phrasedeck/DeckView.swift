import SwiftData
import SwiftUI

/// One page per swipe, each a 3-column grid of phrase buttons and a "+" tile. Long-press a
/// button to edit or delete it. The iPad grid is #12.
struct DeckView: View {
    @Query(sort: \Page.order) private var pages: [Page]
    @Environment(\.modelContext) private var context
    @State private var editing: PhraseEditor.Target?
    @State private var editingPages = false

    private let columns = Array(repeating: GridItem(.flexible(), spacing: 12), count: 3)

    var body: some View {
        Group {
            if pages.isEmpty {
                ContentUnavailableView {
                    Label("No pages", systemImage: "square.grid.3x3")
                } actions: {
                    Button("Add a page") { editingPages = true }
                }
            } else {
                TabView {
                    ForEach(pages) { page in
                        ScrollView {
                            LazyVGrid(columns: columns, spacing: 12) {
                                ForEach(page.sortedPhrases) { phrase in
                                    PhraseButton(phrase: phrase,
                                                 edit: { editing = .existing(phrase) },
                                                 delete: { remove(phrase) })
                                }
                                addTile(page)
                            }
                            .padding()
                        }
                        .navigationTitle(page.name)
                    }
                }
                .tabViewStyle(.page(indexDisplayMode: pages.count > 1 ? .always : .never))
            }
        }
        .toolbar {
            ToolbarItem(placement: .topBarLeading) {
                Button("Pages", systemImage: "rectangle.stack") { editingPages = true }
                    .accessibilityIdentifier("deck.pages")
            }
        }
        .sheet(item: $editing) { PhraseEditor(target: $0) }
        .sheet(isPresented: $editingPages) { PageEditor() }
    }

    private func addTile(_ page: Page) -> some View {
        Button { editing = .new(page) } label: {
            Image(systemName: "plus")
                .font(.title2.weight(.semibold))
                .frame(maxWidth: .infinity, minHeight: 96)
                .padding(8)
                .foregroundStyle(.secondary)
                .background(.quaternary, in: .rect(cornerRadius: 16))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Add a phrase to \(page.name)")
        .accessibilityIdentifier("deck.add")
    }

    private func remove(_ phrase: Phrase) {
        withAnimation { Deck.delete(phrase, in: context) }
        try? context.save()
    }
}
