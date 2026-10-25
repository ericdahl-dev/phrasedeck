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
    @Query private var savedSpots: [SavedSpot]
    /// Arrange mode (#81): Buttons jiggle and move, taps don't type, Done ends it.
    @State private var arranging = false
    @Environment(\.scenePhase) private var scenePhase

    /// Arrange mode is reachable only in DEBUG builds launched with -arrangeEnabled until dragging
    /// works (#81, slice 3), so a release never shows half of it.
    static var arrangeEnabled: Bool {
        #if DEBUG
        ProcessInfo.processInfo.arguments.contains("-arrangeEnabled")
        #else
        false
        #endif
    }

    private var layoutWidth: LayoutWidth { sizeClass == .regular ? .regular : .compact }

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
                                    .frame(maxWidth: .infinity, alignment: .top)
                                    // A tap on empty space ends Arrange mode; Buttons take their own taps.
                                    .contentShape(.rect)
                                    .onTapGesture { if arranging { arranging = false } }
                            }
                            .accessibilityIdentifier("deck.grid")
                            .accessibilityValue("\(columns) columns")
                            .navigationTitle(page.name)
                        }
                    }
                    .tabViewStyle(.page(indexDisplayMode: pages.count > 1 ? .always : .never))
                    // Alongside the page swipe, not instead of it. Off in Arrange mode, where a
                    // change in column count would reflow everything under the finger.
                    .simultaneousGesture(pinch(width: geo.size.width), including: arranging ? .subviews : .all)
                    .sensoryFeedback(.selection, trigger: columns)
                }
            }
        }
        .toolbar {
            if arranging {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { arranging = false }
                        .accessibilityIdentifier("arrange.done")
                }
            } else {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Pages", systemImage: "rectangle.stack") { editingPages = true }
                        .accessibilityIdentifier("deck.pages")
                }
            }
        }
        .sensoryFeedback(.impact(weight: .medium), trigger: arranging) { _, now in now }
        .onChange(of: arranging) { _, now in
            if now {
                AccessibilityNotification.Announcement(
                    "Editing Deck. Use actions to move Buttons. Done is in the toolbar.").post()
            }
        }
        .onChange(of: scenePhase) { _, phase in if phase != .active { arranging = false } }
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
                         in: DeckSize.range(for: sizeClass, width: width))
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
                                                in: DeckSize.range(for: sizeClass, width: width))
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

    /// The page's Buttons at their Spots for this Layout width, or packed by Size (#51) when it has
    /// none, with the "+" tile after them (hidden in Arrange mode).
    private func grid(_ page: Page, columns: Int, cell: CGSize) -> some View {
        let phrases = Dictionary(uniqueKeysWithValues: page.sortedPhrases.map { ($0.persistentModelID, $0) })
        let spots = Deck.spots(on: page, width: layoutWidth, from: savedSpots)
        let items = page.sortedPhrases.map {
            Arrangement.Item(id: Tile.phrase($0.persistentModelID), columns: $0.sizeColumns, rows: $0.sizeRows,
                             spot: spots[$0.uid])
        } + (arranging ? [] : [Arrangement.Item(id: Tile.add, columns: 1, rows: 1, spot: nil)])
        let placed = Arrangement.resolve(items, columns: columns)
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
                                         edit: { editing = .existing(phrase) }, delete: { remove(phrase) },
                                         editDeck: Self.arrangeEnabled ? { arranging = true } : nil,
                                         arrange: arranging ? role(phrase, at: spot, on: page, columns: columns) : nil)
                        }
                    case .add:
                        addTile(page)
                    }
                }
                .frame(width: size.width, height: size.height)
                .offset(x: CGFloat(spot.x) * step.width, y: CGFloat(spot.y) * step.height)
                // VoiceOver reads Buttons as they're shown, row by row, not in order.
                .accessibilitySortPriority(-Double(spot.y * columns + spot.x))
            }
        }
        .accessibilityElement(children: .contain)
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

    /// A Button's move actions in Arrange mode. Each move saves at once and is announced.
    private func role(_ phrase: Phrase, at shown: Packing.Placement<Tile>, on page: Page,
                      columns: Int) -> PhraseButton.ArrangeRole {
        PhraseButton.ArrangeRole(
            spot: Spot(column: shown.x, row: shown.y),
            nudges: Arrangement.nudges(for: shown, columns: columns),
            otherPages: pages.filter { $0 !== page }.map(\.name),
            nudge: { nudge in
                let target = nudge.target(from: shown)
                do {
                    let shifted = try withAnimation(.snappy) {
                        try Deck.move(phrase, to: target, on: page, width: layoutWidth, columns: columns, in: context)
                    }
                    let to = Deck.layout(of: page, width: layoutWidth, columns: columns, in: context)
                        .first { $0.id == phrase.uid }.map { Spot(column: $0.x, row: $0.y) } ?? target
                    let others = shifted == 0 ? "" : shifted == 1 ? ". 1 Button moved" : ". \(shifted) Buttons moved"
                    AccessibilityNotification.Announcement(
                        "Moved to row \(to.row + 1), column \(to.column + 1)\(others)").post()
                } catch {
                    saveError = error.localizedDescription
                }
            },
            moveToPage: { name in
                guard let target = pages.first(where: { $0.name == name && $0 !== page }) else { return }
                var draft = PhraseDraft(phrase)
                draft.page = target
                do { try Deck.save(draft, into: phrase) } catch { saveError = error.localizedDescription }
            })
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

    /// On the iPhone, pinching in reaches 5 columns, or in landscape as many as fit at about
    /// this width each. The iPad stays at 3 to 8.
    static let narrowestColumn: CGFloat = 72

    static func range(for sizeClass: UserInterfaceSizeClass?, width: CGFloat) -> ClosedRange<Int> {
        guard sizeClass != .regular else { return 3...8 }
        return 2...max(5, Int((width - 2 * padding + spacing) / (narrowestColumn + spacing)))
    }

    /// Today's layout: 3 columns on the iPhone, and on the iPad as many 150-point columns as fit.
    static func defaultColumns(for sizeClass: UserInterfaceSizeClass?, width: CGFloat) -> Int {
        guard sizeClass == .regular else { return 3 }
        let fit = Int((width - 2 * padding + spacing) / (iPadDefaultWidth + spacing))
        return fit.clamped(to: range(for: sizeClass, width: width))
    }

    /// The column count a pinch of `scale` snaps to, starting from `start`. Pinching out
    /// (scale above 1) makes fewer, bigger buttons.
    static func columns(pinch scale: CGFloat, from start: Int, in range: ClosedRange<Int>) -> Int {
        guard scale > 0 else { return range.upperBound }
        return Int((CGFloat(start) / scale).rounded()).clamped(to: range)
    }

    /// The stored count clamped to what fits now, or the default when never set (0).
    static func columns(stored: Int, default fallback: Int, in range: ClosedRange<Int>) -> Int {
        stored > 0 ? stored.clamped(to: range) : fallback
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

    /// Label size for a Button of any Size: it grows with the smaller side, so a wide button
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
