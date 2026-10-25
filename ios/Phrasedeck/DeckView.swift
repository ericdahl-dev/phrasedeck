import CoreData
import SwiftData
import SwiftUI

/// One page per swipe, each a grid of phrase buttons and a "+" tile. Long-press a button to
/// edit or delete it; pinch to change how many columns there are.
struct DeckView: View {
    @Query(sort: \Page.order) private var pages: [Page]
    @Environment(\.modelContext) private var context
    @State private var editing: PhraseEditor.Target?
    @State private var editingPages = false
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
                    let cell = CGSize(width: DeckSize.columnWidth(width: geo.size.width, columns: columns),
                                      height: DeckSize.metrics(columnWidth: DeckSize.columnWidth(
                                          width: geo.size.width, columns: columns)).height)
                    TabView {
                        ForEach(pages) { page in
                            ScrollView {
                                grid(page, columns: columns, cell: cell)
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
        // The other device's starter deck can arrive after this one seeded its own.
        .onReceive(NotificationCenter.default.publisher(for: .NSPersistentStoreRemoteChange)
            .receive(on: RunLoop.main)) { _ in try? Seed.mergeDuplicates(context) }
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

    private enum Tile: Hashable {
        case phrase(PersistentIdentifier)
        case add
    }

    /// The page's buttons packed by span (#51), with the "+" tile as a trailing 1×1.
    private func grid(_ page: Page, columns: Int, cell: CGSize) -> some View {
        let phrases = Dictionary(uniqueKeysWithValues: page.sortedPhrases.map { ($0.persistentModelID, $0) })
        let items = page.sortedPhrases.map {
            Packing.Item(id: Tile.phrase($0.persistentModelID), columns: $0.spanColumns, rows: $0.spanRows)
        } + [Packing.Item(id: Tile.add, columns: 1, rows: 1)]
        let placed = Packing.pack(items, columns: columns)
        let step = CGSize(width: cell.width + DeckSize.spacing, height: cell.height + DeckSize.spacing)
        let rows = CGFloat(Packing.rows(of: placed))
        return ZStack(alignment: .topLeading) {
            ForEach(placed, id: \.id) { spot in
                let size = CGSize(width: CGFloat(spot.columns) * step.width - DeckSize.spacing,
                                  height: CGFloat(spot.rows) * step.height - DeckSize.spacing)
                Group {
                    switch spot.id {
                    case .phrase(let id):
                        if let phrase = phrases[id] {
                            PhraseButton(phrase: phrase, metrics: DeckSize.metrics(button: size),
                                         pinchGuard: pinchGuard,
                                         edit: { editing = .existing(phrase) }, delete: { remove(phrase) })
                        }
                    case .add:
                        addTile(page)
                    }
                }
                .frame(width: size.width, height: size.height)
                .offset(x: CGFloat(spot.x) * step.width, y: CGFloat(spot.y) * step.height)
            }
        }
        .frame(width: CGFloat(columns) * step.width - DeckSize.spacing,
               height: max(0, rows * step.height - DeckSize.spacing), alignment: .topLeading)
    }

    private func addTile(_ page: Page) -> some View {
        Button { editing = .new(page) } label: {
            Image(systemName: "plus")
                .font(.title2.weight(.semibold))
                .frame(maxWidth: .infinity, maxHeight: .infinity)
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

    /// Label size for a button of any span: it grows with the smaller side, so a wide button
    /// doesn't get text taller than it is.
    static func metrics(button size: CGSize) -> (height: CGFloat, font: CGFloat) {
        (size.height, metrics(columnWidth: min(size.width, size.height / 0.86)).font)
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
