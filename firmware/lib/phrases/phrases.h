// The panel's own Buttons (#98): each a label and the Phrase it types. Not synced with the iPhone
// Deck. Edited from the panel's web page; stored as JSON in LittleFS.
#pragma once

#include <string>
#include <vector>

namespace phrases {

// Caps, so a page can't fill the flash or the screen. The Helper takes up to 10,000 characters;
// 2,000 bytes is plenty for a Phrase typed from a desk panel.
constexpr size_t kMaxButtons = 48;
constexpr size_t kMaxLabel = 40;   // bytes
constexpr size_t kMaxText = 2000;  // bytes

struct Button {
    std::string label;
    std::string text;  // the Phrase
};

class List {
  public:
    size_t size() const { return buttons_.size(); }
    const Button& at(size_t i) const { return buttons_[i]; }

    // add and edit refuse anything over the caps, or an empty label or Phrase, and then change nothing.
    bool add(const std::string& label, const std::string& text);
    bool edit(size_t i, const std::string& label, const std::string& text);
    bool remove(size_t i);
    bool move(size_t from, size_t to);  // to is the index it ends up at

    // {"buttons":[{"label":..,"text":..},..]}. fromJson replaces the list only if the whole file
    // parses and every Button fits the caps; otherwise it changes nothing.
    std::string toJson() const;
    bool fromJson(const std::string& json);

  private:
    std::vector<Button> buttons_;
};

}  // namespace phrases
