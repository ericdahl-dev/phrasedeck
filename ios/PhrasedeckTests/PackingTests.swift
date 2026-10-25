import Testing
@testable import Phrasedeck

/// Dense packing of buttons with spans into a page `columns` cells wide that grows down (#51).
struct PackingTests {
    typealias Item = Packing.Item<Int>

    static func items(_ spans: [(Int, Int)]) -> [Item] {
        spans.enumerated().map { Item(id: $0.offset, columns: $0.element.0, rows: $0.element.1) }
    }

    /// (x, y, columns, rows) per id, for compact expectations.
    static func spots(_ placements: [Packing.Placement<Int>]) -> [Int: [Int]] {
        Dictionary(uniqueKeysWithValues: placements.map { ($0.id, [$0.x, $0.y, $0.columns, $0.rows]) })
    }

    @Test func nothingPacksToNothing() {
        #expect(Packing.pack([Item](), columns: 3).isEmpty)
        #expect(Packing.rows(of: [Packing.Placement<Int>]()) == 0)
    }

    @Test func onesFillRowByRow() {
        let p = Packing.pack(Self.items([(1, 1), (1, 1), (1, 1), (1, 1)]), columns: 3)
        let s = Self.spots(p)
        #expect(s[0] == [0, 0, 1, 1])
        #expect(s[2] == [2, 0, 1, 1])
        #expect(s[3] == [0, 1, 1, 1])
        #expect(Packing.rows(of: p) == 2)
        #expect(p.map(\.id) == [0, 1, 2, 3])
    }

    @Test func smallButtonsFillTheColumnBesideABigOne() {
        let s = Self.spots(Packing.pack(Self.items([(2, 2), (1, 1), (1, 1)]), columns: 3))
        #expect(s[0] == [0, 0, 2, 2])
        #expect(s[1] == [2, 0, 1, 1])
        #expect(s[2] == [2, 1, 1, 1])
    }

    @Test func aBigButtonGoesToTheFirstRowWithRoom() {
        let s = Self.spots(Packing.pack(Self.items([(1, 1), (1, 1), (1, 1), (2, 2)]), columns: 3))
        #expect(s[3] == [0, 1, 2, 2])
    }

    /// Pinned on purpose (dense): a later small button moves back into a hole left before a wide one.
    @Test func laterSmallButtonsFillEarlierHoles() {
        let s = Self.spots(Packing.pack(Self.items([(1, 1), (3, 1), (1, 1)]), columns: 3))
        #expect(s[1] == [0, 1, 3, 1])
        #expect(s[2] == [1, 0, 1, 1])
    }

    @Test func tallButtonsKeepTheirRows() {
        let s = Self.spots(Packing.pack(Self.items([(1, 3), (1, 1)]), columns: 2))
        #expect(s[0] == [0, 0, 1, 3])
        #expect(s[1] == [1, 0, 1, 1])
    }

    @Test func spansWiderThanThePageAreClampedForDisplayOnly() {
        let input = Self.items([(3, 2), (1, 1)])
        let s = Self.spots(Packing.pack(input, columns: 2))
        #expect(s[0] == [0, 0, 2, 2])
        #expect(s[1] == [0, 2, 1, 1])
        #expect(input[0].columns == 3)
    }

    /// Many random decks: every button is placed once, inside the columns, with no overlaps.
    @Test(arguments: [1, 2, 3, 5, 8])
    func randomDecksNeverOverlap(columns: Int) {
        var rng = SplitMix(seed: UInt64(columns))
        for _ in 0..<300 {
            let input = Self.items((0..<Int.random(in: 0...40, using: &rng)).map { _ in
                (Int.random(in: 1...3, using: &rng), Int.random(in: 1...3, using: &rng))
            })
            let placed = Packing.pack(input, columns: columns)
            #expect(placed.map(\.id).sorted() == input.map(\.id))
            var used = Set<[Int]>()
            for p in placed {
                #expect(p.x >= 0 && p.y >= 0 && p.x + p.columns <= columns)
                for x in p.x..<(p.x + p.columns) {
                    for y in p.y..<(p.y + p.rows) { #expect(used.insert([x, y]).inserted) }
                }
            }
            // All-1×1 decks leave no holes at all.
            if input.allSatisfy({ $0.columns == 1 && $0.rows == 1 }) {
                #expect(used.count == input.count)
            }
        }
    }
}

/// A seeded generator, so the random test is the same on every run.
struct SplitMix: RandomNumberGenerator {
    var seed: UInt64
    mutating func next() -> UInt64 {
        seed &+= 0x9E3779B97F4A7C15
        var z = seed
        z = (z ^ (z >> 30)) &* 0xBF58476D1CE4E5B9
        z = (z ^ (z >> 27)) &* 0x94D049BB133111EB
        return z ^ (z >> 31)
    }
}
