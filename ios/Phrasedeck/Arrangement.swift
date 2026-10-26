import Foundation

/// Where a Button sits on its Page: the column and row of its top-left Square (#81).
struct Spot: Hashable, Sendable {
    let column: Int
    let row: Int
}

/// Shows a Page from the Spots its Buttons have at one Layout width (#81, spec section 2). A Page
/// with no Spots there is packed from `order`, as before.
enum Arrangement {
    struct Item<ID: Hashable>: Hashable {
        let id: ID
        let columns: Int
        let rows: Int
        let spot: Spot?
    }

    /// Placements for `items`, given in `order`.
    static func resolve<ID: Hashable>(_ items: [Item<ID>], columns: Int) -> [Packing.Placement<ID>] {
        guard items.contains(where: { $0.spot != nil }) else {
            return Packing.pack(items.map { Packing.Item(id: $0.id, columns: $0.columns, rows: $0.rows) },
                                columns: columns)
        }
        let columns = max(1, columns)
        var grid = Grid(columns: columns)
        var placed: [ID: Packing.Placement<ID>] = [:]
        /// Places the item at the first free Spot at or after `from` (its own Spot, pulled in to fit).
        func place(_ item: Item<ID>, from: Spot?) {
            let w = min(max(1, item.columns), columns), h = max(1, item.rows)
            let start = from.map { Spot(column: min(max(0, $0.column), columns - w), row: max(0, $0.row)) }
                ?? grid.afterLast
            let at = grid.firstFree(from: start.column, start.row, w, h)
            grid.fill(at.column, at.row, w, h)
            placed[item.id] = Packing.Placement(id: item.id, x: at.column, y: at.row, columns: w, rows: h)
        }
        // Spots in reading order, ties to the earlier in order; then Buttons without one, in order.
        let spotted = items.enumerated().compactMap { i, item in item.spot.map { (i, item, $0) } }
            .sorted { ($0.2.row, $0.2.column, $0.0) < ($1.2.row, $1.2.column, $1.0) }
        for (_, item, spot) in spotted { place(item, from: spot) }
        for item in items where item.spot == nil { place(item, from: nil) }
        return items.compactMap { placed[$0.id] }
    }

    /// A move action: one Square in a direction (spec section 11).
    enum Nudge: CaseIterable {
        case left, right, up, down

        func target<ID>(from p: Packing.Placement<ID>) -> Spot {
            switch self {
            case .left: Spot(column: p.x - 1, row: p.y)
            case .right: Spot(column: p.x + 1, row: p.y)
            case .up: Spot(column: p.x, row: p.y - 1)
            case .down: Spot(column: p.x, row: p.y + 1)
            }
        }
    }

    /// The move actions that apply where the Button is shown. Rows are unbounded, so down always does.
    static func nudges<ID>(for p: Packing.Placement<ID>, columns: Int) -> [Nudge] {
        Nudge.allCases.filter {
            switch $0 {
            case .left: p.x > 0
            case .right: p.x + p.columns < columns
            case .up: p.y > 0
            case .down: true
            }
        }
    }

    /// Every Button's Spot after `id` is dropped on `target`. Starts from `pickup`, the layout when
    /// the Button was picked up, so moving back to where it started puts every neighbor back.
    static func move<ID: Hashable>(_ id: ID, to target: Spot, from pickup: [Packing.Placement<ID>],
                                   columns: Int) -> [ID: Spot] {
        let columns = max(1, columns)
        // A pickup from a wider Page (before a pinch) is narrowed and pulled in to fit this one;
        // otherwise a Button wider than the Page never finds a free Spot.
        let pickup = pickup.map { p in
            let w = min(max(1, p.columns), columns)
            return Packing.Placement(id: p.id, x: min(max(0, p.x), columns - w), y: max(0, p.y), columns: w,
                                     rows: max(1, p.rows))
        }
        var spots = Dictionary(uniqueKeysWithValues: pickup.map { ($0.id, Spot(column: $0.x, row: $0.y)) })
        guard let moved = pickup.first(where: { $0.id == id }) else { return spots }
        let w = min(moved.columns, columns)
        let to = Spot(column: min(max(0, target.column), columns - w), row: max(0, target.row))
        func overlaps(_ p: Packing.Placement<ID>) -> Bool {
            let across = p.x < to.column + w && to.column < p.x + p.columns
            let down = p.y < to.row + moved.rows && to.row < p.y + p.rows
            return across && down
        }
        let others = pickup.filter { $0.id != id }
        // The moved Button's old Spot is empty; Buttons it doesn't cover stay put.
        var grid = Grid(columns: columns)
        for p in others where !overlaps(p) { grid.fill(p.x, p.y, p.columns, p.rows) }
        grid.fill(to.column, to.row, w, moved.rows)
        spots[id] = to
        for p in others.filter(overlaps).sorted(by: { ($0.y, $0.x) < ($1.y, $1.x) }) {
            let at = grid.firstFree(from: p.x, p.y, p.columns, p.rows)
            grid.fill(at.column, at.row, p.columns, p.rows)
            spots[p.id] = at
        }
        return spots
    }
}

/// Which Squares of a Page are taken. Rows are unbounded; a row past the end is empty.
private struct Grid {
    let columns: Int
    private var used: [[Bool]] = []
    /// The Square after the last taken one, in reading order.
    private(set) var afterLast = Spot(column: 0, row: 0)

    init(columns: Int) { self.columns = columns }

    func isFree(_ x: Int, _ y: Int, _ w: Int, _ h: Int) -> Bool {
        guard x >= 0, y >= 0, x + w <= columns else { return false }
        for row in y..<(y + h) where row < used.count {
            for column in x..<(x + w) where used[row][column] { return false }
        }
        return true
    }

    /// The first Spot at or after (x, y), in reading order, where a w x h Button fits.
    func firstFree(from x: Int, _ y: Int, _ w: Int, _ h: Int) -> Spot {
        var (x, y) = (x, y)
        // Past the last used row everything is free, so this ends there at the latest.
        while !isFree(x, y, w, h) {
            x += 1
            if x + w > columns { (x, y) = (0, y + 1) }
        }
        return Spot(column: x, row: y)
    }

    mutating func fill(_ x: Int, _ y: Int, _ w: Int, _ h: Int) {
        while used.count < y + h { used.append(Array(repeating: false, count: columns)) }
        for row in y..<(y + h) { for column in x..<(x + w) { used[row][column] = true } }
        let last = (y + h - 1) * columns + x + w - 1
        if last >= afterLast.row * columns + afterLast.column {
            afterLast = Spot(column: (last + 1) % columns, row: (last + 1) / columns)
        }
    }
}

extension Arrangement {
    /// The Square a dragged Button is headed for: the one nearest its top-left corner, kept on the
    /// Page and far enough left for its `width`. Below the last row is fine (spec section 5).
    static func target(topLeft: (x: Double, y: Double), step: (width: Double, height: Double), columns: Int,
                       width: Int) -> Spot {
        let column = Int((topLeft.x / step.width).rounded())
        let row = Int((topLeft.y / step.height).rounded())
        return Spot(column: min(max(0, column), max(0, columns - width)), row: max(0, row))
    }
}

/// A drag target counts once the finger has rested on it for `hold`, so a fast sweep doesn't
/// shuffle the Page (spec section 5).
struct Dwell {
    static let hold: TimeInterval = 0.15

    /// The target the Page has reflowed for.
    private(set) var accepted: Spot?
    /// The target under the finger, not yet held long enough.
    private(set) var candidate: Spot?
    private var since = Date.distantPast

    mutating func update(_ spot: Spot, at now: Date) {
        if spot != candidate {
            (candidate, since) = (spot, now)
        }
        if now.timeIntervalSince(since) >= Self.hold { accepted = spot }
    }
}
