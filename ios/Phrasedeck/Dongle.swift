import Foundation

/// The Phrasedeck dongle's Bluetooth protocol (#198, #202), as `dongle/README.md` has it: the
/// GATT service, the frames a Send is written in, the Token as the dongle shows it, and the
/// Result it notifies. Pure, with no CoreBluetooth, so every byte is unit tested.
///
/// ```
/// Begin  01 id flags lenLo lenHi token[16] text...   flags bit 0: Send Enter
/// Data   02 id text...                               appended until len bytes are in
/// Stop   03 id                                       cancels Send id
/// Result id code [character]                          notified once per Send
/// ```
enum Dongle {
    /// The name the dongle advertises.
    static let name = "Phrasedeck dongle"

    static let serviceUUID = "6B1D0000-5E2A-4C6E-9A51-2F1C8D7E4A10"
    /// Written, one frame per write; encrypted, with MITM.
    static let sendUUID = "6B1D0001-5E2A-4C6E-9A51-2F1C8D7E4A10"
    /// Notified, one Result per Send.
    static let resultUUID = "6B1D0002-5E2A-4C6E-9A51-2F1C8D7E4A10"
    /// Read, encrypted with MITM: 32 hex digits as ASCII text, for 2 minutes after the bond.
    static let tokenUUID = "6B1D0003-5E2A-4C6E-9A51-2F1C8D7E4A10"

    /// The longest Phrase the dongle takes, in UTF-8 bytes.
    static let maxPhrase = 8192
    /// op, id, flags, length (2) and the Token.
    static let beginHeader = 5 + 16
    /// op and id.
    static let dataHeader = 2

    /// What the dongle says became of a Send: the Result's code.
    enum Code: Equatable, Sendable {
        case typed
        /// A Stop, the cable pulled or the Computer asleep, partway: Maybe typed.
        case stoppedPartway
        /// No Computer on USB, or stopped before the first key: Not typed.
        case notStarted
        /// A character a US keyboard can't type; nothing was typed.
        case cantType(String)
        case wrongToken
        /// Another Send is typing.
        case busy
        /// A frame that doesn't fit.
        case badFrame
        case tooLong
        /// A code this app doesn't know, from newer firmware.
        case unknown(UInt8)
    }

    /// One Result notify: which Send, and what became of it.
    struct Result: Equatable, Sendable {
        let id: UInt8
        let code: Code
    }

    /// Why a Bluetooth Send didn't end Typed. `Outcome` words them; which are Not typed and which
    /// are Maybe typed is said on each.
    enum Failure: Error, Equatable, Sendable {
        /// Not typed: Bluetooth is off or this iPhone has none.
        case bluetoothOff
        /// Not typed: the app isn't allowed Bluetooth.
        case bluetoothDenied
        /// Not typed: no connection before the Begin.
        case notInRange
        /// Not typed: the dongle refused the Begin, or the bond didn't finish (a wrong passkey).
        case notPaired
        /// Not typed: no Token kept for this dongle, or the dongle showed none.
        case noToken
        /// Not typed: the link's writes can't carry a Begin.
        case writeTooShort(Int)
        /// Maybe typed: the link dropped after the Begin went out.
        case lost
        /// Maybe typed: the Send went out, and no Result came back in time.
        case noResult
        /// The dongle's Result, any code but Typed; a Phrase too long is refused here before sending.
        case result(Code)
    }

    /// The writes for one Send, each at most `writeLength` bytes. A Begin carries as much text as
    /// fits, so a short Phrase is one write. A split may fall inside a character: the dongle
    /// puts the bytes back together before it types.
    static func frames(id: UInt8, text: String, enter: Bool, token: Data, writeLength: Int) throws -> [Data] {
        guard token.count == 16 else { throw Failure.noToken }
        let bytes = Array(text.utf8)
        guard bytes.count <= maxPhrase else { throw Failure.result(.tooLong) }
        guard writeLength > beginHeader else { throw Failure.writeTooShort(writeLength) }

        var begin = Data([0x01, id, enter ? 0x01 : 0x00, UInt8(bytes.count & 0xFF), UInt8(bytes.count >> 8)])
        begin.append(token)
        var at = min(bytes.count, writeLength - beginHeader)
        begin.append(contentsOf: bytes[..<at])
        var frames = [begin]
        while at < bytes.count {
            let end = min(bytes.count, at + writeLength - dataHeader)
            frames.append(Data([0x02, id]) + bytes[at..<end])
            at = end
        }
        return frames
    }

    /// Ends Send `id`, received or typing.
    static func stop(id: UInt8) -> Data { Data([0x03, id]) }

    /// The 16 raw bytes a Begin carries, from the Token's 32 hex digits; nil for anything else.
    static func token(hex: String) -> Data? {
        let digits = Array(hex.utf8)
        guard digits.count == 32 else { return nil }
        var token = Data(capacity: 16)
        for pair in stride(from: 0, to: 32, by: 2) {
            guard let high = nibble(digits[pair]), let low = nibble(digits[pair + 1]) else { return nil }
            token.append(high << 4 | low)
        }
        return token
    }

    /// The Token characteristic's value, as the app keeps it: 32 lowercase hex digits. It reads
    /// empty outside its 2 minutes after the bond, and that, like anything else, is no Token.
    static func tokenHex(fromCharacteristic data: Data) -> String? {
        guard let text = String(data: data, encoding: .ascii)?.lowercased(), token(hex: text) != nil else { return nil }
        return text
    }

    /// A Result notify; nil when it's too short to name a Send.
    static func result(_ data: Data) -> Result? {
        let bytes = [UInt8](data)
        guard bytes.count >= 2 else { return nil }
        let code: Code = switch bytes[1] {
        case 0x00: .typed
        case 0x01: .stoppedPartway
        case 0x02: .notStarted
        case 0x03: .cantType(String(decoding: bytes[2...], as: UTF8.self))
        case 0x04: .wrongToken
        case 0x05: .busy
        case 0x06: .badFrame
        case 0x07: .tooLong
        case let other: .unknown(other)
        }
        return Result(id: bytes[0], code: code)
    }

    private static func nibble(_ digit: UInt8) -> UInt8? {
        switch digit {
        case UInt8(ascii: "0")...UInt8(ascii: "9"): digit - UInt8(ascii: "0")
        case UInt8(ascii: "a")...UInt8(ascii: "f"): digit - UInt8(ascii: "a") + 10
        case UInt8(ascii: "A")...UInt8(ascii: "F"): digit - UInt8(ascii: "A") + 10
        default: nil
        }
    }
}
