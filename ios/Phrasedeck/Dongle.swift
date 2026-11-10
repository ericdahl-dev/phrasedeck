import Foundation

/// The Phrasedeck dongle's Bluetooth protocol (#198, #202). Not built yet.
enum Dongle {
    static let name = "Phrasedeck dongle"

    enum Code: Equatable, Sendable {
        case typed, stoppedPartway, notStarted, cantType(String), wrongToken, busy, badFrame, tooLong
        case unknown(UInt8)
    }

    struct Result: Equatable, Sendable {
        let id: UInt8
        let code: Code
    }

    enum Failure: Error, Equatable, Sendable {
        case bluetoothOff, bluetoothDenied, notInRange, notPaired, noToken
        case writeTooShort(Int)
        case lost, noResult
        case result(Code)
    }

    static func frames(id: UInt8, text: String, enter: Bool, token: Data, writeLength: Int) throws -> [Data] { [] }
    static func stop(id: UInt8) -> Data { Data() }
    static func token(hex: String) -> Data? { nil }
    static func tokenHex(fromCharacteristic data: Data) -> String? { nil }
    static func result(_ data: Data) -> Result? { nil }
}
