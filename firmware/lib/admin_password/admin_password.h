// The panel's admin password (#98): set on the touchscreen, stored in NVS as salt + iteration
// count + PBKDF2-HMAC-SHA256. The KDF is passed in: mbedTLS on the board, a stand-in in host tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace admin_password {

constexpr size_t kMinLength = 8;
constexpr uint32_t kMinIterations = 10000;
constexpr uint32_t kMaxIterations = 1000000;  // a corrupt count must not hang check() for hours
constexpr size_t kSaltLen = 16;
constexpr size_t kHashLen = 32;
constexpr size_t kBlobLen = kSaltLen + 4 + kHashLen;

using Kdf = void (*)(const std::string& password, const uint8_t* salt, size_t saltLen, uint32_t iterations,
                     uint8_t* out /* kHashLen */);

struct Record {
    uint8_t salt[kSaltLen] = {};
    uint32_t iterations = 0;
    uint8_t hash[kHashLen] = {};
};

// False, leaving r alone, if the password is too short or the count outside kMinIterations..kMaxIterations.
inline bool make(const std::string& password, const uint8_t* salt, uint32_t iterations, Kdf kdf, Record& r) {
    if (password.size() < kMinLength || iterations < kMinIterations || iterations > kMaxIterations) return false;
    Record next;
    std::memcpy(next.salt, salt, kSaltLen);
    next.iterations = iterations;
    kdf(password, next.salt, kSaltLen, iterations, next.hash);
    r = next;
    return true;
}

// Constant-time compare of the hashes.
inline bool check(const Record& r, const std::string& password, Kdf kdf) {
    if (r.iterations < kMinIterations || r.iterations > kMaxIterations) return false;
    uint8_t h[kHashLen];
    kdf(password, r.salt, kSaltLen, r.iterations, h);
    uint8_t diff = 0;
    for (size_t i = 0; i < kHashLen; ++i) diff |= static_cast<uint8_t>(h[i] ^ r.hash[i]);
    return diff == 0;
}

// Blob layout: salt, iterations (little endian), hash.
inline void toBlob(const Record& r, uint8_t* blob) {
    std::memcpy(blob, r.salt, kSaltLen);
    for (int i = 0; i < 4; ++i) blob[kSaltLen + i] = static_cast<uint8_t>(r.iterations >> (8 * i));
    std::memcpy(blob + kSaltLen + 4, r.hash, kHashLen);
}

inline bool fromBlob(const uint8_t* blob, size_t len, Record& r) {
    if (len != kBlobLen) return false;
    Record next;
    std::memcpy(next.salt, blob, kSaltLen);
    for (int i = 0; i < 4; ++i) next.iterations |= static_cast<uint32_t>(blob[kSaltLen + i]) << (8 * i);
    std::memcpy(next.hash, blob + kSaltLen + 4, kHashLen);
    if (next.iterations < kMinIterations || next.iterations > kMaxIterations) return false;
    r = next;
    return true;
}

}  // namespace admin_password
