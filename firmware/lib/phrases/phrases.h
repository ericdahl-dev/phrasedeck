// The panel's own Buttons (#98): each a label and the Phrase it types. Not synced with the iPhone
// Deck. Edited from the panel's web page; stored as JSON in LittleFS.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phrases {

// Caps, so a page can't fill the flash or the screen. The Helper takes up to 10,000 characters;
// 2,000 bytes is plenty for a Phrase typed from a desk panel.
constexpr size_t kMaxButtons = 48;
constexpr size_t kMaxLabel = 40;   // bytes
constexpr size_t kMaxText = 2000;  // bytes
constexpr uint32_t kMaxCounter = 0x7FFFFFFF;  // ids and edit counts stop here, so they never wrap to 0

struct Button {
    uint32_t id = 0;    // never changes and is never reused, so the Helper can tell Phrases apart
    uint32_t edit = 1;  // goes up on each edit; the Helper notifies on a counter it hasn't seen
    uint32_t confirmedEdit = 0;  // the edit the user OKed on the panel; 0 = never
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

    // Eric (#98): a Phrase added or edited on the web page is shown and confirmed on the panel
    // before its first send. True for an index out of range, so nothing sends unasked.
    bool needsConfirm(size_t i) const { return i >= buttons_.size() || buttons_[i].confirmedEdit != buttons_[i].edit; }
    void confirmed(size_t i) {
        if (i < buttons_.size()) buttons_[i].confirmedEdit = buttons_[i].edit;
    }

    // {"next":N,"buttons":[{"id":..,"edit":..,"label":..,"text":..},..]}. fromJson replaces the list only if the whole file
    // parses and every Button fits the caps; otherwise it changes nothing.
    std::string toJson() const;
    bool fromJson(const std::string& json);

  private:
    std::vector<Button> buttons_;
    uint32_t nextId_ = 1;
};

}  // namespace phrases
