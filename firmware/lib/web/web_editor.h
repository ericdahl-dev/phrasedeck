// The web editor's Button changes (#98): parsed from a form, applied to the list on the LVGL loop,
// and the page that shows them. Host-tested; no Arduino, httpd or LVGL.
#pragma once

#include <cstdint>
#include <string>

#include "phrases.h"

namespace web {

// One change from the page. Every kind but Add names its Button by id and edit, so a page that
// is out of date changes nothing.
struct Edit {
    enum class Kind { Add, Change, Remove, Up, Down } kind = Kind::Add;
    uint32_t id = 0;
    uint32_t edit = 0;
    std::string label;
    std::string text;  // CRLF from the browser already turned into LF
};

// Parses a form body (do=add|change|remove|up|down, id, edit, label, text). False if malformed.
bool editFrom(const std::string& body, Edit& e);

// Applies e to the list; false, changing nothing, if its Button is gone or changed, or the list refuses it.
bool apply(phrases::List& list, const Edit& e);

// & < > " ' as entities, so text is never markup.
std::string html(const std::string& s);

// The editor: each Button with its Phrase (Enter shown as ↵) and a form to change, move or
// delete it, then a form to add one. note, if any, shows on top (already plain text).
std::string editorPage(const phrases::List& list, const std::string& note = "");
// The same page in parts, so the server sends it a Button at a time instead of building it whole.
std::string editorHead(const phrases::List& list, const std::string& note);
std::string editorButton(const phrases::Button& b);
std::string editorTail();

}  // namespace web
