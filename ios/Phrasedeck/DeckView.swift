import SwiftData
import SwiftUI

/// One page per swipe, each a grid of phrase buttons and a "+" tile. Long-press a button to
/// edit or delete it; pinch to change how many columns there are.
struct DeckView: View {
    @Query(sort: \Page.order) private var pages: [Page]
    @Environment(\.modelContext) private var context
    @State private var editing: PhraseEditor.Target?
    @State private var editingPages = false
    @State private var saveError: String?
    @Environment(\.horizontalSizeClass) private var sizeClass
    @AppStorage(DeckSize.compactKey) private var compactColumns = 0
    @AppStorage(DeckSize.regularKey) private var regularColumns = 0
    /// The live column count while a pinch is under way.
    @State private var pinchColumns: Int?
    @State private var pinchStart: Int?
    @State private var pinchGuard = PinchGuard()

    var body: some View {
        Group {
            if pages.isEmpty {
                ContentUnavailableView {
                    Label("No pages", systemImage: "square.grid.3x3")
                } actions: {
                    Button("Add a page") { editingPages = true }
                }
            } else {
                GeometryReader { geo in
                    let columns = shownColumns(width: geo.size.width)
                    let metrics = DeckSize.metrics(
                        columnWidth: DeckSize.columnWidth(width: geo.size.width, columns: columns))
                    TabView {
                        ForEach(pages) { page in
                            ScrollView {
                                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: DeckSize.spacing),
                                                         count: columns),
                                          spacing: DeckSize.spacing) {
                                    ForEach(page.sortedPhrases) { phrase in
                                        PhraseButton(phrase: phrase, metrics: metrics, pinchGuard: pinchGuard,
                                                     edit: { editing = .existing(phrase) },
                                                     delete: { remove(phrase) })
                                    }
                                    addTile(page, height: metrics.height)
                                }
                                .padding(DeckSize.padding)
                                .animation(.snappy, value: columns)
                            }
                            .accessibilityIdentifier("deck.grid")
                            .accessibilityValue("\(columns) columns")
                            .navigationTitle(page.name)
                        }
                    }
                    .tabViewStyle(.page(indexDisplayMode: pages.count > 1 ? .always : .never))
                    // Alongside the page swipe, not instead of it.
                    .simultaneousGesture(pinch(width: geo.size.width))
                    .sensoryFeedback(.selection, trigger: columns)
                }
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
        .saveFailureAlert($saveError)
    }

    private var storedColumns: Int {
        get { sizeClass == .regular ? regularColumns : compactColumns }
        nonmutating set {
            if sizeClass == .regular { regularColumns = newValue } else { compactColumns = newValue }
        }
    }

    private func savedColumns(width: CGFloat) -> Int {
        DeckSize.columns(stored: storedColumns,
                         default: DeckSize.defaultColumns(for: sizeClass, width: width),
                         in: DeckSize.range(for: sizeClass))
    }

    private func shownColumns(width: CGFloat) -> Int {
        pinchColumns ?? savedColumns(width: width)
    }

    /// Previews the column count live and snaps it on release. Taps are held off during the
    /// pinch and just after, so a pinch that starts on a button never sends it.
    private func pinch(width: CGFloat) -> some Gesture {
        MagnifyGesture()
            .onChanged { value in
                pinchGuard.isPinching = true
                let start = pinchStart ?? savedColumns(width: width)
                pinchStart = start
                pinchColumns = DeckSize.columns(pinch: value.magnification, from: start,
                                                in: DeckSize.range(for: sizeClass))
            }
            .onEnded { _ in
                if let pinchColumns { storedColumns = pinchColumns }
                pinchColumns = nil
                pinchStart = nil
                pinchGuard.ended()
            }
    }

    private func addTile(_ page: Page, height: CGFloat) -> some View {
        Button { editing = .new(page) } label: {
            Image(systemName: "plus")
                .font(.title2.weight(.semibold))
                .frame(maxWidth: .infinity, minHeight: height - 16)
                .padding(8)
                .foregroundStyle(.secondary)
                .background(.quaternary, in: .rect(cornerRadius: 16))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Add a phrase to \(page.name)")
        .accessibilityIdentifier("deck.add")
    }

    private func remove(_ phrase: Phrase) {
        do { try withAnimation { try Deck.delete(phrase, in: context) } } catch {
            saveError = error.localizedDescription
        }
    }
}

/// How many columns the deck shows and how big its buttons are. Pinching changes the column
/// count within each device's limits; the count is stored per device, never synced (#38).
enum DeckSize {
    static let spacing: CGFloat = 12
    static let padding: CGFloat = 16
    /// The old iPad grid fit as many columns of at least this width as it could.
    static let iPadDefaultWidth: CGFloat = 150

    static func range(for sizeClass: UserInterfaceSizeClass?) -> ClosedRange<Int> {
        sizeClass == .regular ? 3...8 : 2...5
    }

    /// Today's layout: 3 columns on the iPhone, and on the iPad as many 150-point columns as fit.
    static func defaultColumns(for sizeClass: UserInterfaceSizeClass?, width: CGFloat) -> Int {
        guard sizeClass == .regular else { return 3 }
        let fit = Int((width - 2 * padding + spacing) / (iPadDefaultWidth + spacing))
        return fit.clamped(to: range(for: sizeClass))
    }

    /// The column count a pinch of `scale` snaps to, starting from `start`. Pinching out
    /// (scale above 1) makes fewer, bigger buttons.
    static func columns(pinch scale: CGFloat, from start: Int, in range: ClosedRange<Int>) -> Int {
        guard scale > 0 else { return range.upperBound }
        return Int((CGFloat(start) / scale).rounded()).clamped(to: range)
    }

    /// The stored count, or the default when nothing valid is stored (0 means "never set").
    static func columns(stored: Int, default fallback: Int, in range: ClosedRange<Int>) -> Int {
        range.contains(stored) ? stored : fallback
    }

    static let compactKey = "deck.columns.compact"
    static let regularKey = "deck.columns.regular"

    static func key(for sizeClass: UserInterfaceSizeClass?) -> String {
        sizeClass == .regular ? regularKey : compactKey
    }

    /// Button height and label size for a column width, so buttons keep roughly today's shape
    /// and labels grow with them.
    static func metrics(columnWidth: CGFloat) -> (height: CGFloat, font: CGFloat) {
        (max(56, columnWidth * 0.86), min(34, max(13, columnWidth * 0.17)))
    }

    static func columnWidth(width: CGFloat, columns: Int) -> CGFloat {
        (width - 2 * padding - spacing * CGFloat(columns - 1)) / CGFloat(columns)
    }
}

extension Comparable {
    func clamped(to range: ClosedRange<Self>) -> Self {
        min(max(self, range.lowerBound), range.upperBound)
    }
}

/// Tells phrase buttons to ignore taps during a pinch and for a moment after it.
@MainActor
final class PinchGuard {
    var isPinching = false
    private var quietUntil = Date.distantPast

    func ended() {
        isPinching = false
        quietUntil = Date().addingTimeInterval(0.5)
    }

    var blocksTaps: Bool { isPinching || Date() < quietUntil }
}
