#include "phone_link.h"

#include <algorithm>

namespace phone_link {

std::vector<std::vector<uint8_t>> frames(uint8_t id, const Token& token, const std::string& text, bool enter,
                                         size_t maxWrite) {
    std::vector<std::vector<uint8_t>> out;
    if (maxWrite < kBeginHeader || maxWrite <= kMoreHeader || text.size() > kMaxText) return out;
    const size_t len = text.size();
    std::vector<uint8_t> begin = {enter ? kBeginEnter : kBegin, id, static_cast<uint8_t>(len),
                                  static_cast<uint8_t>(len >> 8)};
    begin.insert(begin.end(), token.begin(), token.end());
    size_t at = std::min(len, maxWrite - kBeginHeader);
    begin.insert(begin.end(), text.begin(), text.begin() + at);
    out.push_back(std::move(begin));
    while (at < len) {
        const size_t n = std::min(len - at, maxWrite - kMoreHeader);
        std::vector<uint8_t> more = {kMore, id, static_cast<uint8_t>(at), static_cast<uint8_t>(at >> 8)};
        more.insert(more.end(), text.begin() + at, text.begin() + at + n);
        out.push_back(std::move(more));
        at += n;
    }
    return out;
}

Fed Assembler::feed(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) return fail();
    if (data[0] == kBegin || data[0] == kBeginEnter) {
        if (len < kBeginHeader) return fail();
        const size_t total = data[2] | (data[3] << 8);
        if (total > kMaxText) return fail();
        id_ = data[1];
        enter_ = data[0] == kBeginEnter;
        std::copy(data + 4, data + kBeginHeader, token_.begin());
        total_ = total;
        text_.clear();
        text_.reserve(total);
        open_ = true;
        return append(data + kBeginHeader, len - kBeginHeader);
    }
    if (data[0] == kMore) {
        if (!open_ || len < kMoreHeader || data[1] != id_) return fail();
        const size_t at = data[2] | (data[3] << 8);
        if (at != text_.size()) return fail();  // a lost or repeated write
        return append(data + kMoreHeader, len - kMoreHeader);
    }
    return fail();
}

Fed Assembler::append(const uint8_t* data, size_t len) {
    if (text_.size() + len > total_) return fail();
    text_.append(reinterpret_cast<const char*>(data), len);
    if (text_.size() < total_) return Fed::More;
    open_ = false;
    return Fed::Done;
}

bool trusted(const Link& link) {
    return link.phoneIdentity && link.encrypted && link.authenticated && link.bonded && link.keySize == 16 &&
           link.storedPhone;
}

Verdict check(const Link& link, const Token& sent, const Token& issued) {
    if (!trusted(link)) return Verdict::NotTrusted;
    uint8_t diff = 0, any = 0;  // every byte, whatever the first difference: no timing hint
    for (size_t i = 0; i < kTokenLen; ++i) {
        diff |= sent[i] ^ issued[i];
        any |= issued[i];
    }
    return diff == 0 && any != 0 ? Verdict::Send : Verdict::WrongToken;
}

std::vector<uint8_t> status(uint8_t id, send::Outcome outcome, const std::string& message, size_t maxLen) {
    const uint8_t o = outcome == send::Outcome::Typed        ? kTyped
                      : outcome == send::Outcome::MaybeTyped ? kMaybeTyped
                                                             : kNotTyped;
    std::vector<uint8_t> out = {id, o};
    size_t n = maxLen > 2 ? std::min(message.size(), maxLen - 2) : 0;
    // Back off any UTF-8 continuation bytes, so the cut never splits a character.
    if (n < message.size()) {
        while (n > 0 && (static_cast<uint8_t>(message[n]) & 0xC0) == 0x80) --n;
    }
    out.insert(out.end(), message.begin(), message.begin() + n);
    return out;
}

Advertise advertise(bool keyboardWants, bool phoneConnected, bool phoneBonded, bool phoneWindowOpen) {
    if (keyboardWants) return Advertise::Keyboard;
    if (!phoneConnected && (phoneBonded || phoneWindowOpen)) return Advertise::Phone;
    return Advertise::None;
}

Fed Assembler::fail() {
    open_ = false;
    text_.clear();
    total_ = 0;
    return Fed::Bad;
}

}  // namespace phone_link
