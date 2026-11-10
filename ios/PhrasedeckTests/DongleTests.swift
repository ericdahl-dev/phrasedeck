import Foundation
import Testing
@testable import Phrasedeck

/// The dongle's Bluetooth protocol (#202), byte for byte as `dongle/README.md` (#198) has it:
/// frames, splitting to the write length, the Token as text, and Result decoding. Pure, so no radio.
struct DongleFrameTests {
    /// The Token from the README's example, as its 32 hex digits and as the 16 raw bytes a Begin carries.
    static let tokenHex = "5f0c1a2b3c4d5e6f708192a3b4c5d6e1"
    static let token = Data([0x5f, 0x0c, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f,
                             0x70, 0x81, 0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe1])

    /// The README's nRF Connect example: `01 01 00 02 00 <Token> 68 69` types "hi" with no Enter.
    @Test func hiIsOneBeginAsTheREADMEHasIt() throws {
        let frames = try Dongle.frames(id: 1, text: "hi", enter: false, token: Self.token, writeLength: 182)

        #expect(frames == [Data([0x01, 0x01, 0x00, 0x02, 0x00]) + Self.token + Data([0x68, 0x69])])
    }

    @Test func sendEnterSetsFlagsBitZero() throws {
        let frames = try Dongle.frames(id: 7, text: "hi", enter: true, token: Self.token, writeLength: 182)

        let begin = try #require(frames.first)
        #expect(frames.count == 1)
        #expect(begin[1] == 7)
        #expect(begin[2] == 0x01)
    }

    /// The length is the Phrase's UTF-8 bytes, little-endian, not its characters.
    @Test func theLengthIsUTF8BytesLittleEndian() throws {
        let text = String(repeating: "é", count: 150)  // 300 bytes
        let frames = try Dongle.frames(id: 1, text: text, enter: false, token: Self.token, writeLength: 182)

        let begin = try #require(frames.first)
        #expect(begin[3] == 0x2C)
        #expect(begin[4] == 0x01)
    }

    /// A Begin carries as much text as fits after its 21-byte header.
    @Test func aBeginThatExactlyFillsTheWriteIsOneFrame() throws {
        let text = String(repeating: "a", count: 182 - 21)
        let frames = try Dongle.frames(id: 1, text: text, enter: false, token: Self.token, writeLength: 182)

        #expect(frames.count == 1)
        #expect(frames.first?.count == 182)
    }

    @Test func oneByteMoreGoesInADataFrame() throws {
        let text = String(repeating: "a", count: 182 - 21) + "b"
        let frames = try Dongle.frames(id: 9, text: text, enter: false, token: Self.token, writeLength: 182)

        try #require(frames.count == 2)
        #expect(frames[0].count == 182)
        #expect(frames[1] == Data([0x02, 9, 0x62]))
    }

    /// Every frame fits the write length, Data frames name the Send, and the pieces put back
    /// together are the Phrase's bytes, even when a split falls inside a character.
    @Test(arguments: [182, 244, 22, 100])
    func aLongPhraseSplitsToTheWriteLengthAndReassembles(writeLength: Int) throws {
        let text = String(repeating: "Grüße, ", count: 400)  // 3600 bytes, two-byte characters throughout
        let frames = try Dongle.frames(id: 3, text: text, enter: false, token: Self.token, writeLength: writeLength)

        #expect(frames.allSatisfy { $0.count <= writeLength })
        #expect(frames.dropFirst().allSatisfy { $0.prefix(2) == Data([0x02, 3]) })
        // Every frame but the last is full, so a Phrase takes as few writes as it can.
        #expect(frames.dropLast().allSatisfy { $0.count == writeLength })
        let payload = try #require(frames.first).dropFirst(21) + frames.dropFirst().flatMap { $0.dropFirst(2) }
        #expect(Data(payload) == Data(text.utf8))
    }

    @Test func anEmptyPhraseIsABeginWithLengthZero() throws {
        let frames = try Dongle.frames(id: 2, text: "", enter: true, token: Self.token, writeLength: 182)

        #expect(frames == [Data([0x01, 0x02, 0x01, 0x00, 0x00]) + Self.token])
    }

    @Test func thePhraseMayBe8192Bytes() throws {
        let frames = try Dongle.frames(id: 1, text: String(repeating: "a", count: 8192), enter: false,
                                       token: Self.token, writeLength: 182)

        let begin = try #require(frames.first)
        #expect(begin[3] == 0x00)
        #expect(begin[4] == 0x20)
    }

    @Test func aPhraseOver8192BytesIsRefusedBeforeAnyFrame() {
        #expect(throws: Dongle.Failure.result(.tooLong)) {
            try Dongle.frames(id: 1, text: String(repeating: "a", count: 8193), enter: false,
                              token: Self.token, writeLength: 182)
        }
    }

    /// The smallest write that carries a Begin and a byte of text is 22 (ATT's minimum MTU gives 20).
    @Test func aWriteTooShortForABeginIsRefused() throws {
        #expect(throws: Dongle.Failure.writeTooShort(20)) {
            try Dongle.frames(id: 1, text: "hi", enter: false, token: Self.token, writeLength: 20)
        }
        #expect(try Dongle.frames(id: 1, text: "hi", enter: false, token: Self.token, writeLength: 22).count == 2)
    }

    /// The Token goes as 16 raw bytes: 32 ASCII bytes is a frame the dongle refuses (code 06).
    @Test func aTokenThatIsNot16BytesIsRefused() {
        #expect(throws: Dongle.Failure.noToken) {
            try Dongle.frames(id: 1, text: "hi", enter: false, token: Data(Self.tokenHex.utf8), writeLength: 182)
        }
    }

    @Test func stopNamesTheSend() {
        #expect(Dongle.stop(id: 0x2A) == Data([0x03, 0x2A]))
    }
}

struct DongleTokenTests {
    @Test func hexDigitsDecodeTo16Bytes() {
        #expect(Dongle.token(hex: DongleFrameTests.tokenHex) == DongleFrameTests.token)
        #expect(Dongle.token(hex: DongleFrameTests.tokenHex.uppercased()) == DongleFrameTests.token)
    }

    @Test(arguments: ["", "5f0c", String(repeating: "a", count: 31), String(repeating: "a", count: 33),
                      String(repeating: "g", count: 32), "5f0c1a2b3c4d5e6f708192a3b4c5d6e" + "é"])
    func anythingElseIsNoToken(hex: String) {
        #expect(Dongle.token(hex: hex) == nil)
    }

    /// The Token characteristic reads as ASCII text; it's kept as lowercase hex.
    @Test func theCharacteristicsTextBecomesTheKeptHex() {
        #expect(Dongle.tokenHex(fromCharacteristic: Data(DongleFrameTests.tokenHex.uppercased().utf8))
                == DongleFrameTests.tokenHex)
    }

    /// After its 2 minutes the characteristic reads empty: there's nothing to keep.
    @Test func anEmptyOrBadCharacteristicIsNoToken() {
        #expect(Dongle.tokenHex(fromCharacteristic: Data()) == nil)
        #expect(Dongle.tokenHex(fromCharacteristic: DongleFrameTests.token) == nil)
    }
}

struct DongleResultTests {
    @Test(arguments: [
        (UInt8(0x00), Dongle.Code.typed),
        (0x01, .stoppedPartway),
        (0x02, .notStarted),
        (0x04, .wrongToken),
        (0x05, .busy),
        (0x06, .badFrame),
        (0x07, .tooLong),
        (0x42, .unknown(0x42)),
    ])
    func eachCodeDecodes(byte: UInt8, code: Dongle.Code) {
        #expect(Dongle.result(Data([0x09, byte])) == Dongle.Result(id: 0x09, code: code))
    }

    /// Code 03 names the character a US keyboard can't type, in UTF-8.
    @Test func aRefusedCharacterIsNamed() {
        #expect(Dongle.result(Data([0x02, 0x03, 0xC3, 0xA9])) == Dongle.Result(id: 2, code: .cantType("é")))
    }

    @Test func tooShortIsNoResult() {
        #expect(Dongle.result(Data()) == nil)
        #expect(Dongle.result(Data([0x01])) == nil)
    }
}
