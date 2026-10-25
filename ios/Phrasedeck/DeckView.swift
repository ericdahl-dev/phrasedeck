import SwiftData
import SwiftUI

/// One page per swipe, each a 3-column grid of phrase buttons. The iPad
/// grid is #12.
struct DeckView: View {
    @Query(sort: \Page.order) private var pages: [Page]

    private let columns = Array(repeating: GridItem(.flexible(), spacing: 12), count: 3)

    var body: some View {
        TabView {
            ForEach(pages) { page in
                ScrollView {
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(page.sortedPhrases) { phrase in
                            PhraseButton(phrase: phrase)
                        }
                    }
                    .padding()
                }
                .navigationTitle(page.name)
            }
        }
        .tabViewStyle(.page(indexDisplayMode: pages.count > 1 ? .always : .never))
    }
}
