#include "phrases.h"

#include <ArduinoJson.h>

namespace phrases {
namespace {

bool fits(const std::string& label, const std::string& text) {
    return !label.empty() && label.size() <= kMaxLabel && !text.empty() && text.size() <= kMaxText;
}

}  // namespace

bool List::add(const std::string& label, const std::string& text) {
    if (buttons_.size() >= kMaxButtons || !fits(label, text)) return false;
    buttons_.push_back({label, text});
    return true;
}

bool List::edit(size_t i, const std::string& label, const std::string& text) {
    if (i >= buttons_.size() || !fits(label, text)) return false;
    buttons_[i] = {label, text};
    return true;
}

bool List::remove(size_t i) {
    if (i >= buttons_.size()) return false;
    buttons_.erase(buttons_.begin() + static_cast<long>(i));
    return true;
}

bool List::move(size_t from, size_t to) {
    if (from >= buttons_.size() || to >= buttons_.size()) return false;
    Button b = buttons_[from];
    buttons_.erase(buttons_.begin() + static_cast<long>(from));
    buttons_.insert(buttons_.begin() + static_cast<long>(to), b);
    return true;
}

std::string List::toJson() const {
    JsonDocument doc;
    JsonArray arr = doc["buttons"].to<JsonArray>();
    for (const Button& b : buttons_) {
        JsonObject o = arr.add<JsonObject>();
        o["label"] = b.label;
        o["text"] = b.text;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

bool List::fromJson(const std::string& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    JsonArrayConst arr = doc["buttons"].as<JsonArrayConst>();
    if (arr.isNull()) return false;
    List next;
    for (JsonObjectConst o : arr) {
        if (!next.add(o["label"] | "", o["text"] | "")) return false;
    }
    *this = next;
    return true;
}

}  // namespace phrases
