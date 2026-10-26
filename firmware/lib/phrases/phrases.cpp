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
    buttons_.push_back({nextId_++, 1, 0, label, text});
    return true;
}

bool List::edit(size_t i, const std::string& label, const std::string& text) {
    if (i >= buttons_.size() || !fits(label, text)) return false;
    buttons_[i].label = label;
    buttons_[i].text = text;
    ++buttons_[i].edit;
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
    doc["next"] = nextId_;
    JsonArray arr = doc["buttons"].to<JsonArray>();
    for (const Button& b : buttons_) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = b.id;
        o["edit"] = b.edit;
        o["confirmed"] = b.confirmedEdit;
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
    next.nextId_ = doc["next"] | 1u;
    for (JsonObjectConst o : arr) {
        const Button b{o["id"] | 0u, o["edit"] | 0u, o["confirmed"] | 0u, o["label"] | "", o["text"] | ""};
        if (b.id == 0 || b.edit == 0 || next.buttons_.size() >= kMaxButtons || !fits(b.label, b.text)) return false;
        for (const Button& other : next.buttons_) {
            if (other.id == b.id) return false;
        }
        next.buttons_.push_back(b);
        if (b.id >= next.nextId_) next.nextId_ = b.id + 1;
    }
    *this = next;
    return true;
}

}  // namespace phrases
