// Pairing the panel with a Helper: the keypad state for the Pairing code, and the short Pin check
// (docs/launch/lan-threat-checklist.md, "Panel pairing").
#pragma once

#include <cstdint>
#include <string>

namespace pairing {

class CodeEntry {
  public:
    static constexpr size_t kLength = 6;

    void digit(char d) {
        if (d >= '0' && d <= '9' && code_.size() < kLength) code_ += d;
    }
    void back() {
        if (!code_.empty()) code_.pop_back();
    }
    bool complete() const { return code_.size() == kLength; }
    const std::string& code() const { return code_; }

  private:
    std::string code_;
};

// True if the short Pin typed from `phrased pair` (exactly 16 hex once spaces are dropped, any case)
// starts certSha256, the lowercase hex SHA-256 of the Helper's TLS certificate. Only then may the
// Pairing code be sent.
bool matchesShortPin(const std::string& typed, const std::string& certSha256);

// A SHA-256 digest as 64 lowercase hex characters, the form the Helper prints and stores.
std::string hexLower(const uint8_t digest[32]);

// True if certSha256 is exactly the stored Pin (64 hex). Every request after pairing uses this,
// never the short Pin. Constant time.
bool matchesPin(const std::string& pin, const std::string& certSha256);

// Reads /pair's reply. True, with token set, only for a 200 whose JSON has a token and whose
// certSHA256 is checkedCert, the certificate the panel just checked in the TLS handshake.
bool readPairReply(int status, const std::string& body, const std::string& checkedCert, std::string& token);

}  // namespace pairing
