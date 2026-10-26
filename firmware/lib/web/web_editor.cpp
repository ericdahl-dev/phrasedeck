#include "web_editor.h"

#include "web.h"

namespace web {
namespace {

// Digits only, and below kMaxCounter, so "", "-1" or "1x" never become an id.
bool number(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 10) return false;
    uint64_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    if (n > phrases::kMaxCounter) return false;
    out = static_cast<uint32_t>(n);
    return true;
}

std::string lf(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\r') out += s[i];
        else if (i + 1 >= s.size() || s[i + 1] != '\n') out += '\n';  // a lone CR is an Enter too
    }
    return out;
}

}  // namespace

bool editFrom(const std::string& body, Edit& e) {
    const std::string kind = formValue(body, "do");
    if (kind == "add") e.kind = Edit::Kind::Add;
    else if (kind == "change") e.kind = Edit::Kind::Change;
    else if (kind == "remove") e.kind = Edit::Kind::Remove;
    else if (kind == "up") e.kind = Edit::Kind::Up;
    else if (kind == "down") e.kind = Edit::Kind::Down;
    else return false;
    e.label = formValue(body, "label");
    e.text = lf(formValue(body, "text"));
    if (e.kind == Edit::Kind::Add) {
        e.id = e.edit = 0;
        return true;
    }
    return number(formValue(body, "id"), e.id) && number(formValue(body, "edit"), e.edit);
}

bool apply(phrases::List& list, const Edit& e) {
    if (e.kind == Edit::Kind::Add) return list.add(e.label, e.text);
    const size_t i = list.find(e.id, e.edit);
    if (i >= list.size()) return false;
    switch (e.kind) {
        case Edit::Kind::Change: return list.edit(i, e.label, e.text);
        case Edit::Kind::Remove: return list.remove(i);
        case Edit::Kind::Up: return i > 0 && list.move(i, i - 1);
        case Edit::Kind::Down: return i + 1 < list.size() && list.move(i, i + 1);
        default: return false;
    }
}

std::string html(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string editorHead(const phrases::List& list, const std::string& note) {
    std::string p;
    if (!note.empty()) p += "<p class=note>" + html(note) + "</p>";
    p += "<p>" + std::to_string(list.size()) + " of " + std::to_string(phrases::kMaxButtons) +
         ". A Button added or changed here shows its Phrase on the panel and asks before its first Send.</p>";
    return p;
}

std::string editorButton(const phrases::Button& b) {
    return "<details><summary>" + html(b.label) + "</summary><pre>" +
           html(phrases::visible(b.text, "\xE2\x86\xB5", "\xE2\x86\x92")) + "</pre>"
           "<form method=post action=/edit><input type=hidden name=id value=" + std::to_string(b.id) +
           "><input type=hidden name=edit value=" + std::to_string(b.edit) + ">"
           "<p><label>Label<br><input name=label maxlength=40 value=\"" + html(b.label) + "\"></label></p>"
           // The newline after <textarea> is dropped by the browser, so a Phrase starting with Enter keeps it.
           "<p><label>Phrase<br><textarea name=text rows=4 maxlength=2000>\n" + html(b.text) + "</textarea></label></p>"
           "<p><button name=do value=change>Save</button> <button name=do value=up>Up</button> "
           "<button name=do value=down>Down</button> <button name=do value=remove>Delete</button></p></form></details>";
}

std::string editorTail() {
    return "<h2>Add a Button</h2><form method=post action=/edit><input type=hidden name=do value=add>"
           "<p><label>Label<br><input name=label maxlength=40 required></label></p>"
           "<p><label>Phrase<br><textarea name=text rows=4 maxlength=2000 required></textarea></label></p>"
           "<p><button>Add</button></p></form>"
           "<form method=post action=/logout><p><button>Log out</button></p></form>";
}

std::string editorPage(const phrases::List& list, const std::string& note) {
    std::string p = editorHead(list, note);
    for (size_t i = 0; i < list.size(); ++i) p += editorButton(list.at(i));
    return p + editorTail();
}

}  // namespace web
