/// Dense packing for phrase buttons with spans (#51). A page is `columns` cells wide and grows
/// down. Each button, in order, takes the first spot from the top-left where it fits, so later
/// small buttons fill holes left before bigger ones, and order is kept as far as packing allows.
enum Packing {
    struct Item<ID: Hashable>: Hashable {
        let id: ID
        let columns: Int
        let rows: Int
    }

    struct Placement<ID: Hashable>: Hashable {
        let id: ID
        let x: Int
        let y: Int
        let columns: Int
        let rows: Int
    }

    /// Placements in input order. A span wider than the page is narrowed to fit; the item
    /// itself (the stored span) is left alone.
    static func pack<ID: Hashable>(_ items: [Item<ID>], columns: Int) -> [Placement<ID>] {
        let columns = max(1, columns)
        var used: [[Bool]] = []
        func free(_ x: Int, _ y: Int, _ w: Int, _ h: Int) -> Bool {
            for row in y..<(y + h) where row < used.count {
                for column in x..<(x + w) where used[row][column] { return false }
            }
            return true
        }
        return items.map { item in
            let w = min(max(1, item.columns), columns)
            let h = max(1, item.rows)
            // Row `used.count` is always empty, so the search ends there at the latest.
            var y = 0
            while true {
                if let x = (0...(columns - w)).first(where: { free($0, y, w, h) }) {
                    while used.count < y + h { used.append(Array(repeating: false, count: columns)) }
                    for row in y..<(y + h) { for column in x..<(x + w) { used[row][column] = true } }
                    return Placement(id: item.id, x: x, y: y, columns: w, rows: h)
                }
                y += 1
            }
        }
    }

    /// How many rows the placements cover.
    static func rows<ID>(of placements: [Placement<ID>]) -> Int {
        placements.map { $0.y + $0.rows }.max() ?? 0
    }
}
