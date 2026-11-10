#include "phrase_link.h"

#include <algorithm>

namespace phrase_link {

namespace {
constexpr uint8_t kBegin = 0x01, kData = 0x02, kStop = 0x03;
constexpr size_t kBeginHeader = 5 + 16;  // op, id, flags, length (2), Token
constexpr size_t kDataHeader = 2;        // op, id

Event event(Event::Kind kind, uint8_t id, Code code = Code::Typed) {
    Event e;
    e.kind = kind;
    e.id = id;
    e.code = code;
    return e;
}
}  // namespace

bool sameToken(const uint8_t* given, const Token& want) {
    uint8_t diff = 0;
    for (size_t i = 0; i < want.size(); i++) diff |= given[i] ^ want[i];
    return diff == 0;
}

void Receiver::drop() {
    receiving_ = false;
    text_.clear();  // keeps its capacity: no allocation on the next Send
}

Event Receiver::feed(const uint8_t* data, size_t len, const Token& token) {
    if (len < 2) return {};  // too short to name a Send
    const uint8_t op = data[0], id = data[1];
    const bool current = receiving_ && id == id_;
    if (op == kBegin) {
        drop();  // a new Begin replaces a Send that never finished (the phone retried)
        if (len < kBeginHeader) return event(Event::Kind::Done, id, Code::BadFrame);
        if (!sameToken(data + 5, token)) return event(Event::Kind::Done, id, Code::BadToken);
        const size_t length = data[3] | size_t(data[4]) << 8;
        if (length > kMaxPhrase) return event(Event::Kind::Done, id, Code::TooLong);
        id_ = id;
        enter_ = data[2] & 1;
        length_ = length;
        receiving_ = true;
        return append(data + kBeginHeader, len - kBeginHeader);
    }
    if (op == kData && current) return append(data + kDataHeader, len - kDataHeader);
    if (op == kStop && len == 2) {
        if (!current) return event(Event::Kind::Stop, id);
        drop();
        return event(Event::Kind::Done, id, Code::NotTyped);
    }
    if (current) drop();  // a bad write for the Send in progress ends it
    return event(Event::Kind::Done, id, Code::BadFrame);
}

Event Receiver::append(const uint8_t* bytes, size_t n) {
    if (text_.size() + n > length_) {
        drop();
        return event(Event::Kind::Done, id_, Code::BadFrame);
    }
    text_.append(reinterpret_cast<const char*>(bytes), n);
    if (text_.size() < length_) return {};
    receiving_ = false;
    Event e = event(Event::Kind::Ready, id_);
    e.enter = enter_;
    return e;
}

std::vector<std::vector<uint8_t>> frames(uint8_t id, const std::string& text, bool enter, const Token& token,
                                         size_t maxWrite) {
    std::vector<std::vector<uint8_t>> out;
    std::vector<uint8_t> begin = {kBegin, id, uint8_t(enter ? 1 : 0), uint8_t(text.size() & 0xFF),
                                  uint8_t(text.size() >> 8)};
    begin.insert(begin.end(), token.begin(), token.end());
    size_t at = std::min(text.size(), maxWrite - kBeginHeader);
    begin.insert(begin.end(), text.begin(), text.begin() + at);
    out.push_back(std::move(begin));
    while (at < text.size()) {
        const size_t n = std::min(text.size() - at, maxWrite - kDataHeader);
        std::vector<uint8_t> f = {kData, id};
        f.insert(f.end(), text.begin() + at, text.begin() + at + n);
        out.push_back(std::move(f));
        at += n;
    }
    return out;
}

std::vector<uint8_t> result(uint8_t id, Code code, const std::string& detail) {
    std::vector<uint8_t> out = {id, static_cast<uint8_t>(code)};
    out.insert(out.end(), detail.begin(), detail.end());
    return out;
}

}  // namespace phrase_link
