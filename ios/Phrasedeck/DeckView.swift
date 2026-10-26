import os
import SwiftData
import SwiftUI
import UniformTypeIdentifiers

/// The hold-then-drag path, for a device test (#96): `log collect`, then filter on this subsystem
/// and category.
let dragLog = Logger(subsystem: "dev.ericdahl.phrasedeck", category: "drag")

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
    /// The Button being dragged, if any.
    @State private var carry: Carry?
    /// A Button held long enough for a system drag (#96). It's carried once the finger moves, so
    /// holding alone still opens the menu.
    @State private var lifted: Carry?
    /// True while a finger drags; SwiftUI resets it even when a drag is canceled without onEnded.
    @GestureState private var dragging = false
    @State private var shownPage: PersistentIdentifier?
    /// The Button whose delete is being confirmed, and the Page to keep a gap on in Arrange mode.
    @State private var deleting: (phrase: Phrase, gapOn: (page: Page, columns: Int)?)?
    @State private var deleted = 0
    /// Where each Page's grid sits on screen, so a drag can cross Pages (#81, slice 4).
    @State private var gridOrigins: [PersistentIdentifier: CGPoint] = [:]
    /// Each Page's scroll state and place on screen, for auto-scroll while dragging (spec section 5).
    @State private var scrollPositions: [PersistentIdentifier: ScrollPosition] = [:]
    @State private var scrollGeometries: [PersistentIdentifier: ScrollGeometry] = [:]
    @State private var scrollFrames: [PersistentIdentifier: CGRect] = [:]
    @Environment(\.scenePhase) private var scenePhase


    /// Wider than tall, from the Deck's own size rather than the device, so Split View counts.
    @State private var landscape = false
    /// Portrait and landscape each keep their own layout.
    private var layoutWidth: LayoutWidth { LayoutWidth(regular: sizeClass == .regular, landscape: landscape) }

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
                    let deck = geo.frame(in: .global)
                    TabView(selection: $shownPage) {
                        ForEach(pages) { page in
                            pageScroll(page, columns: columns, cell: cell, deck: deck)
                        }
                    }
                    .tabViewStyle(.page(indexDisplayMode: pages.count > 1 ? .always : .never))
                    // Alongside the page swipe, not instead of it. Off in Arrange mode, where a
                    // change in column count would reflow everything under the finger.
                    .simultaneousGesture(pinch(width: geo.size.width), including: arranging ? .subviews : .all)
                    .sensoryFeedback(.selection, trigger: columns)
                    .overlay(alignment: .topLeading) { carriedButton(deck: deck) }
                    .overlay(alignment: carry?.edge.edge == -1 ? .leading : .trailing) { edgeHighlight }
                    .onGeometryChange(for: Bool.self) { $0.size.width > $0.size.height } action: { landscape = $0 }
                }
            }
        }
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                let page = pages.first { $0.persistentModelID == shownPage } ?? pages.first
                Button("Add Button", systemImage: "plus") { page.map { editing = .new($0) } }
                    .disabled(page == nil)
                    .accessibilityIdentifier("deck.new")
            }
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
        .onChange(of: arranging) { _, now in if !now { carry = nil } }
        // A canceled drag (a system edge swipe, a banner) skips onEnded: drop it without saving,
        // so the next drag can't save this Button.
        // A rotation mid-drag changes the layout under the finger; drop nothing into the new one.
        .onChange(of: layoutWidth) { carry = nil }
        .onChange(of: dragging) { _, now in if !now { withAnimation(.snappy) { carry = nil } } }
        // A finger resting still sends no updates, so the dwell and the edge timer are checked here.
        .task(id: carry == nil) {
            while carry != nil, !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(100))
                tick()
            }
        }
        .sensoryFeedback(.impact(weight: .light), trigger: carry?.page) { old, new in old != nil && new != nil }
        .sheet(item: $editing) { PhraseEditor(target: $0) }
        .sheet(isPresented: $editingPages) { PageEditor() }
        .alert(deleting.map { "Delete \u{201C}\($0.phrase.label)\u{201D}?" } ?? "",
               isPresented: Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } }),
               presenting: deleting) { d in
            Button("Delete", role: .destructive) { confirmDelete(d.phrase, gapOn: d.gapOn) }
            Button("Cancel", role: .cancel) {}
        } message: { _ in
            Text("This removes the Button and its Phrase from your Deck on all your phones and iPads.")
        }
        .sensoryFeedback(.impact(weight: .medium), trigger: deleted)
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

    /// One Page's scrolling grid. Its own function, so the type-checker doesn't time out on body.
    private func pageScroll(_ page: Page, columns: Int, cell: CGSize, deck: CGRect) -> some View {
        ScrollView {
            grid(page, columns: columns, cell: cell, deck: deck)
                .padding(DeckSize.padding)
                .animation(.snappy, value: columns)
                .frame(maxWidth: .infinity, alignment: .top)
                // A tap on empty space ends Arrange mode; Buttons take their own taps.
                .contentShape(.rect)
                .onTapGesture { if arranging { arranging = false } }
        }
        .overlay {
            if page.sortedPhrases.isEmpty && !arranging {
                ContentUnavailableView {
                    Label("No Buttons", systemImage: "square.grid.2x2")
                } description: {
                    Text("Each Button types its Phrase on your Computer.")
                } actions: {
                    Button("Add a Button") { editing = .new(page) }
                        .buttonStyle(.borderedProminent)
                }
            }
        }
        .scrollPosition(Binding(get: { scrollPositions[page.persistentModelID] ?? ScrollPosition() },
                                set: { scrollPositions[page.persistentModelID] = $0 }))
        // Only Arrange mode drags; outside it, scrolling writes no state.
        .onScrollGeometryChange(for: ScrollGeometry?.self) { arranging ? $0 : nil } action: { _, geometry in
            scrollGeometries[page.persistentModelID] = geometry
        }
        .onGeometryChange(for: CGRect.self) { $0.frame(in: .global) } action: {
            scrollFrames[page.persistentModelID] = $0
        }
        .accessibilityIdentifier("deck.grid")
        .accessibilityValue("\(columns) columns")
        .navigationTitle(page.name)
        .tag(Optional(page.persistentModelID))
    }

    private enum Tile: Hashable {
        case phrase(PersistentIdentifier)
        case add
    }

    /// The page's Buttons at their Spots for this Layout width, or packed by Size (#51) when it has
    /// none, with the "+" tile after them in Arrange mode (not while carrying, so no preview lands on it).
    private func grid(_ page: Page, columns: Int, cell: CGSize, deck: CGRect) -> some View {
        let phrases = Dictionary(uniqueKeysWithValues: page.sortedPhrases.map { ($0.persistentModelID, $0) })
        let items = items(page) + (arranging && carry == nil ? [Arrangement.Item(id: Tile.add, columns: 1, rows: 1, spot: nil)] : [])
        let placed = Arrangement.resolve(items, columns: columns)
        let step = CGSize(width: cell.width + DeckSize.spacing, height: cell.height + DeckSize.spacing)
        let rows = CGFloat(Packing.rows(of: placed))
        // The carried Button's preview shows on the Page it's over; the Page it left keeps a gap.
        let here = carry?.page == page.persistentModelID
        let previewRows = here ? carry.map { c in c.preview.map { $0.value.row + c.rows }.max() ?? 0 } ?? 0 : 0
        return ZStack(alignment: .topLeading) {
            ForEach(placed, id: \.id) { spot in
                let size = CGSize(width: CGFloat(spot.columns) * step.width - DeckSize.spacing,
                                  height: CGFloat(spot.rows) * step.height - DeckSize.spacing)
                let carried = carry?.tile == spot.id
                let own = Spot(column: spot.x, row: spot.y)
                let at = here && !carried ? carry?.preview[spot.id] ?? own : own
                Group {
                    switch spot.id {
                    case .phrase(let id):
                        if let phrase = phrases[id] {
                            PhraseButton(phrase: phrase, metrics: DeckSize.metrics(button: size),
                                         pinchGuard: pinchGuard,
                                         edit: { editing = .existing(phrase) }, delete: { deleting = (phrase, nil) },
                                         editDeck: { arranging = true },
                                         arrange: arranging ? role(phrase, at: spot, on: page, columns: columns) : nil,
                                         startDrag: systemCarry(spot, size: size, on: page, step: step,
                                                                columns: columns, deck: deck))
                        }
                    case .add:
                        addTile(page)
                    }
                }
                .frame(width: size.width, height: size.height)
                // While carried it's drawn above the Pages (carriedButton), so it can cross them.
                .opacity(carried ? 0 : 1)
                .transition(.scale.combined(with: .opacity))
                .offset(x: CGFloat(at.column) * step.width, y: CGFloat(at.row) * step.height)
                // In Arrange mode a Button drags at once (spec section 4).
                .highPriorityGesture(carryGesture(spot, size: size, on: page, step: step, columns: columns, deck: deck),
                                     including: arranging && spot.id != .add ? .all : .subviews)
                // VoiceOver reads Buttons as they're shown, row by row, not in order.
                .accessibilitySortPriority(-Double(spot.y * columns + spot.x))
            }
        }
        .accessibilityElement(children: .contain)
        .frame(width: CGFloat(columns) * step.width - DeckSize.spacing,
               height: max(0, max(rows, CGFloat(previewRows)) * step.height - DeckSize.spacing),
               alignment: .topLeading)
        .onGeometryChange(for: CGPoint.self) { $0.frame(in: .global).origin } action: {
            gridOrigins[page.persistentModelID] = $0
        }
        // A Button carried by a system drag, from normal mode (spec section 3, #96).
        .onDrop(of: [.plainText], delegate: CarryDrop(
            moved: { location in
                if let l = lifted {
                    lifted = nil
                    carry = l
                    arranging = true
                    dragLog.notice("carry from lift")
                }
                guard carry != nil, let grid = gridOrigins[page.persistentModelID] else { return }
                carry?.finger = CGPoint(x: grid.x + location.x, y: grid.y + location.y)
                tick()
                dragLog.notice("target \(carry?.target?.column ?? -1) \(carry?.target?.row ?? -1)")
            },
            // Off this grid: show the Page as it was until the drag comes back.
            exited: { carry?.dwell = Dwell(); carry?.target = nil; carry?.preview = [:] },
            dropped: {
                let carrying = carry != nil
                drop()
                return carrying
            }))
    }

    /// Holding a Button lifts it for a system drag; moving the finger then closes the menu, starts
    /// Arrange mode and carries it (spec section 3). The system draws the Button under the finger.
    /// Set in Arrange mode too, so the drag source outlives the switch into it.
    private func systemCarry(_ spot: Packing.Placement<Tile>, size: CGSize, on page: Page, step: CGSize,
                             columns: Int, deck: CGRect) -> () -> Void {
        {
            // An Arrange-mode finger drag already under way is kept.
            if carry?.system ?? true {
                let grid = gridOrigins[page.persistentModelID] ?? .zero
                let grab = CGSize(width: size.width / 2, height: size.height / 2)
                var c = Carry(tile: spot.id, columns: spot.columns, rows: spot.rows, from: page.persistentModelID,
                              page: page.persistentModelID, pickup: Arrangement.resolve(items(page), columns: columns),
                              step: step, columnCount: columns, deck: deck, size: size, grab: grab, system: true)
                // Until the first drop update, the finger is where the Button was.
                c.finger = CGPoint(x: grid.x + CGFloat(spot.x) * step.width + grab.width,
                                   y: grid.y + CGFloat(spot.y) * step.height + grab.height)
                lifted = c
            }
            dragLog.notice("lift at \(spot.x) \(spot.y) arranging=\(arranging)")
        }
    }

    /// The Page's Buttons with their Spots for this Layout width.
    private func items(_ page: Page) -> [Arrangement.Item<Tile>] {
        let ids = Dictionary(uniqueKeysWithValues: page.sortedPhrases.map { ($0.uid, $0.persistentModelID) })
        return Deck.items(of: page, width: layoutWidth, from: savedSpots).compactMap { item in
            ids[item.id].map { Arrangement.Item(id: Tile.phrase($0), columns: item.columns, rows: item.rows, spot: item.spot) }
        }
    }

    /// Arrange mode's drag: the Button follows the finger, the Page under it reflows after a short
    /// rest, holding at a screen edge flips the Page, and letting go saves where it landed.
    private func carryGesture(_ spot: Packing.Placement<Tile>, size: CGSize, on page: Page, step: CGSize,
                              columns: Int, deck: CGRect) -> some Gesture {
        DragGesture(minimumDistance: 8, coordinateSpace: .global)
            .updating($dragging) { _, state, _ in state = true }
            .onChanged { value in
                if carry?.tile != spot.id {
                    let grid = gridOrigins[page.persistentModelID] ?? .zero
                    let corner = CGPoint(x: grid.x + CGFloat(spot.x) * step.width, y: grid.y + CGFloat(spot.y) * step.height)
                    carry = Carry(tile: spot.id, columns: spot.columns, rows: spot.rows, from: page.persistentModelID,
                                  page: page.persistentModelID,
                                  pickup: Arrangement.resolve(items(page), columns: columns),
                                  step: step, columnCount: columns, deck: deck, size: size,
                                  grab: CGSize(width: value.startLocation.x - corner.x,
                                               height: value.startLocation.y - corner.y))
                }
                carry?.finger = value.location
                tick()
            }
            .onEnded { _ in drop() }
    }

    /// Flips the Page if the finger has held at an edge, then aims the carried Button at the Square
    /// under its top-left corner; the Page reflows once that target has held for `Dwell.hold`.
    private func tick() {
        guard var c = carry else { return }
        if let index = pages.firstIndex(where: { $0.persistentModelID == c.page }),
           let next = c.edge.update(x: c.finger.x - c.deck.minX, width: c.deck.width, page: index,
                                    pages: pages.count, at: .now) {
            // The new Page reflows from its own layout, with the Button joining at its end.
            let page = pages[next]
            c.page = page.persistentModelID
            c.pickup = Arrangement.resolve(
                items(page) + [Arrangement.Item(id: c.tile, columns: c.columns, rows: c.rows, spot: nil)],
                columns: c.columnCount)
            (c.dwell, c.target, c.preview) = (Dwell(), nil, [:])
            withAnimation { shownPage = c.page }
        }
        autoScroll(c)
        // A Page just flipped to may not be laid out yet; aim once it is (the next tick).
        guard let grid = gridOrigins[c.page] else {
            carry = c
            return
        }
        let target = Arrangement.target(
            topLeft: (c.finger.x - c.grab.width - grid.x, c.finger.y - c.grab.height - grid.y),
            step: (c.step.width, c.step.height), columns: c.columnCount, width: c.columns,
            maxRow: c.pickup.filter { $0.id != c.tile }.map { $0.y + $0.rows }.max() ?? 0)
        c.target = target
        let before = c.dwell.accepted
        c.dwell.update(target, at: .now)
        if let accepted = c.dwell.accepted, accepted != before {
            c.preview = Arrangement.move(c.tile, to: accepted, from: c.pickup, columns: c.columnCount)
            withAnimation(.snappy) { carry = c }
        } else {
            carry = c
        }
    }

    /// Scrolls the Page under the finger when it's near the top or bottom. The Page grows a row at a
    /// time as the target goes below its last row, so holding at the bottom keeps scrolling.
    private func autoScroll(_ c: Carry) {
        guard let frame = scrollFrames[c.page], let geometry = scrollGeometries[c.page] else { return }
        let step = AutoScroll.step(fingerY: c.finger.y, top: frame.minY, bottom: frame.maxY)
        guard step != 0 else { return }
        let lowest = -geometry.contentInsets.top
        let highest = max(lowest, geometry.contentSize.height - geometry.containerSize.height
                          + geometry.contentInsets.bottom)
        let y = min(max(lowest, geometry.contentOffset.y + step), highest)
        guard y != geometry.contentOffset.y else { return }
        scrollPositions[c.page, default: ScrollPosition()].scrollTo(y: y)
    }

    /// Saves the carried Button where the finger let go, on the Page it's over.
    private func drop() {
        dragLog.notice("drop system=\(carry?.system ?? false) target=\(carry?.target?.column ?? -1) \(carry?.target?.row ?? -1)")
        defer { withAnimation(.snappy) { carry = nil } }
        guard let c = carry, let target = c.target, case .phrase(let id) = c.tile,
              let from = pages.first(where: { $0.persistentModelID == c.from }),
              let to = pages.first(where: { $0.persistentModelID == c.page }),
              let phrase = from.sortedPhrases.first(where: { $0.persistentModelID == id }) else { return }
        do {
            try withAnimation(.snappy) {
                if to === from {
                    _ = try Deck.move(phrase, to: target, on: to, width: layoutWidth, columns: c.columnCount, in: context)
                } else {
                    try Deck.move(phrase, to: target, onto: to, width: layoutWidth, columns: c.columnCount, in: context)
                }
            }
        } catch {
            saveError = error.localizedDescription
        }
    }

    /// The carried Button, drawn above the Pages under the finger.
    @ViewBuilder private func carriedButton(deck: CGRect) -> some View {
        if let c = carry, !c.system, case .phrase(let id) = c.tile,
           let phrase = pages.lazy.flatMap(\.sortedPhrases).first(where: { $0.persistentModelID == id }) {
            PhraseButton(phrase: phrase, metrics: DeckSize.metrics(button: c.size))
                .frame(width: c.size.width, height: c.size.height)
                .scaleEffect(1.05)
                .shadow(radius: 8)
                .offset(x: c.finger.x - c.grab.width - deck.minX, y: c.finger.y - c.grab.height - deck.minY)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
        }
    }

    /// A soft highlight at the edge the finger is holding, filling in until the Page flips.
    @ViewBuilder private var edgeHighlight: some View {
        if carry?.edge.edge != nil {
            Rectangle()
                .fill(.tint.opacity(0.25))
                .frame(width: EdgeFlip.zone)
                .transition(.opacity.animation(.easeIn(duration: EdgeFlip.hold)))
                .allowsHitTesting(false)
        }
    }

    /// A Button being dragged in Arrange mode.
    private struct Carry {
        let tile: Tile
        let columns: Int
        let rows: Int
        /// The Page it was picked up from, and the Page it's over now.
        let from: PersistentIdentifier
        var page: PersistentIdentifier
        /// The Page it's over, as it was when the Button arrived; every reflow starts from it.
        var pickup: [Packing.Placement<Tile>]
        let step: CGSize
        let columnCount: Int
        /// The Deck's frame on screen, for the edges.
        let deck: CGRect
        let size: CGSize
        /// Where in the Button the finger holds it.
        let grab: CGSize
        /// Carried by a system drag (#96), which draws its own preview and drops through `CarryDrop`.
        var system = false
        var finger = CGPoint.zero
        var dwell = Dwell()
        var edge = EdgeFlip()
        /// The Square under the Button now, where it lands if let go.
        var target: Spot?
        /// Every Button's Spot on that Page for the accepted target.
        var preview: [Tile: Spot] = [:]
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
            otherPages: pages.filter { $0 !== page },
            nudge: { nudge in
                do {
                    let result = try withAnimation(.snappy) {
                        try Deck.nudge(phrase, nudge, on: page, width: layoutWidth, columns: columns, in: context)
                    }
                    AccessibilityNotification.Announcement(
                        Deck.moveAnnouncement(to: result.to, shifted: result.shifted)).post()
                } catch {
                    saveError = error.localizedDescription
                }
            },
            moveToPage: { target in
                do { try Deck.moveToPage(phrase, target) } catch { saveError = error.localizedDescription }
            },
            delete: { deleting = (phrase, (page, columns)) })
    }

    /// Deletes the confirmed Button. In Arrange mode the others stay where they are (spec section 9).
    private func confirmDelete(_ phrase: Phrase, gapOn gap: (page: Page, columns: Int)?) {
        do {
            try withAnimation {
                if let gap {
                    try Deck.delete(phrase, keepingGapOn: gap.page, width: layoutWidth, columns: gap.columns, in: context)
                } else {
                    try Deck.delete(phrase, in: context)
                }
            }
            deleted += 1
        } catch {
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

/// Passes a system drag's location in the grid and its drop to the Deck (#96).
private struct CarryDrop: DropDelegate {
    let moved: (CGPoint) -> Void
    let exited: () -> Void
    let dropped: () -> Bool

    func dropEntered(info: DropInfo) { dragLog.notice("dropEntered") }

    func dropUpdated(info: DropInfo) -> DropProposal? {
        moved(info.location)
        return DropProposal(operation: .move)
    }

    func dropExited(info: DropInfo) {
        dragLog.notice("dropExited")
        exited()
    }

    func performDrop(info: DropInfo) -> Bool {
        dragLog.notice("performDrop")
        return dropped()
    }
}
