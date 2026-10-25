import Foundation
import Security

/// Pairing tokens, one Keychain item per computer, keyed by `Helper.id`.
/// Items are this-device-only and never synchronizable, so each device
/// pairs on its own and tokens never reach iCloud.
struct TokenStore: Sendable {
    struct KeychainError: Error, Equatable {
        let status: OSStatus
    }

    var service = "dev.ericdahl.phrasedeck"

    func set(_ token: String, for helperID: UUID) throws {
        let data = Data(token.utf8)
        let status = SecItemUpdate(query(helperID) as CFDictionary,
                                   [kSecValueData as String: data] as CFDictionary)
        if status == errSecItemNotFound {
            var item = query(helperID)
            item[kSecValueData as String] = data
            item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            try check(SecItemAdd(item as CFDictionary, nil))
        } else {
            try check(status)
        }
    }

    func get(_ helperID: UUID) throws -> String? {
        var item = query(helperID)
        item[kSecReturnData as String] = true
        item[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(item as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        try check(status)
        return (result as? Data).flatMap { String(data: $0, encoding: .utf8) }
    }

    func delete(_ helperID: UUID) throws {
        let status = SecItemDelete(query(helperID) as CFDictionary)
        if status != errSecItemNotFound { try check(status) }
    }

    /// The stored item's attributes, for tests that check accessibility.
    func attributes(for helperID: UUID) throws -> [String: Any]? {
        var item = query(helperID)
        item[kSecReturnAttributes as String] = true
        item[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(item as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        try check(status)
        return result as? [String: Any]
    }

    private func query(_ helperID: UUID) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: helperID.uuidString,
         kSecAttrSynchronizable as String: false]
    }

    private func check(_ status: OSStatus) throws {
        guard status == errSecSuccess else { throw KeychainError(status: status) }
    }
}
