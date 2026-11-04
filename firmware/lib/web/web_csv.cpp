#include "web_csv.h"

#include <utility>
#include <vector>

namespace web {
namespace {

std::string field(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
}

// CRLF and a lone CR as LF, as the form does: the panel types LF.
std::string lf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\r') out += s[i];
        else if (i + 1 >= s.size() || s[i + 1] != '\n') out += '\n';
    }
    return out;
}

bool utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        size_t n;
        uint32_t cp;
        if (c < 0x80) n = 0, cp = c;
        else if (c >= 0xC2 && c <= 0xDF) n = 1, cp = c & 0x1F;
        else if (c >= 0xE0 && c <= 0xEF) n = 2, cp = c & 0x0F;
        else if (c >= 0xF0 && c <= 0xF4) n = 3, cp = c & 0x07;
        else return false;
        if (i + n >= s.size()) return false;  // cut short
        for (size_t k = 1; k <= n; ++k) {
            const unsigned char d = s[i + k];
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return false;
        i += n + 1;
    }
    return true;
}

bool control(const std::string& s, bool enterAndTab) {
    for (unsigned char c : s) {
        if (c == 0x7F || (c < 0x20 && !(enterAndTab && (c == '\n' || c == '\t')))) return true;
    }
    return false;
}

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// RFC 4180 records, one at a time. A field's CRLF is kept here; lf() turns it into LF later.
class Reader {
  public:
    explicit Reader(std::string_view s) : s_(s) {
        if (s_.substr(0, 3) == "\xEF\xBB\xBF") s_.remove_prefix(3);  // Excel's BOM
        while (!s_.empty() && (s_.back() == '\n' || s_.back() == '\r')) s_.remove_suffix(1);  // blank trailing lines
    }
    bool done() const { return pos_ >= s_.size(); }
    // The next record's fields; false, with error set, if it's malformed.
    bool next(std::vector<std::string>& fields, std::string& error) {
        fields.clear();
        for (;;) {
            std::string f;
            if (pos_ < s_.size() && s_[pos_] == '"') {
                for (++pos_;; ++pos_) {
                    if (pos_ >= s_.size()) return (error = "has a quote that isn't closed", false);
                    if (s_[pos_] != '"') f += s_[pos_];
                    else if (pos_ + 1 < s_.size() && s_[pos_ + 1] == '"') f += s_[pos_++];
                    else break;
                }
                ++pos_;
                if (pos_ < s_.size() && s_[pos_] != ',' && s_[pos_] != '\r' && s_[pos_] != '\n') {
                    return (error = "has text after a closing quote", false);
                }
            } else {
                for (; pos_ < s_.size() && s_[pos_] != ',' && s_[pos_] != '\r' && s_[pos_] != '\n'; ++pos_) {
                    if (s_[pos_] == '"') return (error = "has a quote inside a field that doesn't start with one", false);
                    f += s_[pos_];
                }
            }
            fields.push_back(std::move(f));
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '\r') ++pos_;
            if (pos_ < s_.size() && s_[pos_] == '\n') ++pos_;
            return true;
        }
    }

  private:
    std::string_view s_;
    size_t pos_ = 0;
};

// Why a row can't be a Button, or empty if it can.
std::string refused(const std::string& label, const std::string& text) {
    if (label.empty()) return "has no label";
    if (text.empty()) return "has no Phrase";
    if (!utf8(label) || !utf8(text)) return "isn't UTF-8 (save the file as CSV UTF-8)";
    if (label.size() > phrases::kMaxLabel) return "has a label over " + std::to_string(phrases::kMaxLabel) + " bytes";
    if (text.size() > phrases::kMaxText) return "has a Phrase over 2,000 bytes";
    if (control(label, false)) return "has a line break or control character in its label";
    if (control(text, true)) return "has a control character in its Phrase";
    return "";
}

bool caseEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        if (x != b[i]) return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// One parameter of a header value like `form-data; name="file"; filename="b.csv"`, unquoted.
// Parameters are split on ';' first, so a filename holding `name=` is never mistaken for one.
bool param(std::string_view value, std::string_view key, std::string_view& out) {
    size_t semi = value.find(';');
    while (semi != std::string_view::npos) {
        value.remove_prefix(semi + 1);
        semi = value.find(';');
        const std::string_view p = trim(value.substr(0, semi));
        const size_t eq = p.find('=');
        if (eq == std::string_view::npos || !caseEqual(trim(p.substr(0, eq)), key)) continue;
        std::string_view v = trim(p.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        out = v;
        return true;
    }
    return false;
}

bool multipartBoundary(std::string_view contentType, std::string_view& boundary) {
    const size_t semi = contentType.find(';');
    if (semi == std::string_view::npos || !caseEqual(trim(contentType.substr(0, semi)), "multipart/form-data")) return false;
    // A boundary is at most 70 characters (RFC 2046); a filename with ';' would need quotes we don't parse.
    return param(contentType, "boundary", boundary) && !boundary.empty() && boundary.size() <= 70;
}

// The name="..." of a part's Content-Disposition header.
bool partName(std::string_view headers, std::string_view& name) {
    while (!headers.empty()) {
        const size_t eol = headers.find("\r\n");
        const std::string_view line = headers.substr(0, eol);
        headers.remove_prefix(eol == std::string_view::npos ? headers.size() : eol + 2);
        const size_t colon = line.find(':');
        if (colon != std::string_view::npos && caseEqual(trim(line.substr(0, colon)), "content-disposition")) {
            return param(line.substr(colon + 1), "name", name);
        }
    }
    return false;
}

}  // namespace

std::string csvHead() { return "\xEF\xBB\xBFlabel,phrase\r\n"; }

std::string csvRow(const phrases::Button& b) { return field(b.label) + "," + field(b.text) + "\r\n"; }

bool importFrom(const std::string& contentType, std::string_view body, Import& im) {
    std::string_view boundary;
    if (!multipartBoundary(contentType, boundary)) return false;
    std::string_view kind, file;
    bool haveKind = false, haveFile = false;
    const std::string delim = "--" + std::string(boundary);
    const std::string next = "\r\n" + delim;
    if (body.substr(0, delim.size()) != delim) return false;
    size_t pos = delim.size();
    for (;;) {
        if (body.substr(pos, 2) == "--") break;  // the closing boundary
        if (body.substr(pos, 2) != "\r\n") return false;
        pos += 2;
        const size_t headersEnd = body.find("\r\n\r\n", pos);
        if (headersEnd == std::string_view::npos) return false;
        const std::string_view headers = body.substr(pos, headersEnd - pos);
        pos = headersEnd + 4;
        const size_t end = body.find(next, pos);
        if (end == std::string_view::npos) return false;
        const std::string_view data = body.substr(pos, end - pos);
        pos = end + next.size();
        std::string_view name;
        if (!partName(headers, name)) return false;
        if (name == "do" && !haveKind) kind = data, haveKind = true;
        if (name == "file" && !haveFile) file = data, haveFile = true;
    }
    if (!haveKind || !haveFile) return false;
    if (kind == "replace") im.mode = Import::Mode::Replace;
    else if (kind == "append") im.mode = Import::Mode::Append;
    else return false;
    im.csv = file;
    return true;
}

bool apply(const phrases::List& list, const Import& im, phrases::List& out, std::string& error) {
    phrases::List next = list;  // keeps the id counter, so imported Buttons get new ids
    if (im.mode == Import::Mode::Replace) next.clear();
    Reader r(im.csv);
    std::vector<std::string> fields;
    const auto fail = [&error](size_t row, const std::string& why) {
        error = "Row " + std::to_string(row) + " " + why + ".";
        return false;
    };
    if (r.done() || !r.next(fields, error) || fields.size() != 2 || lower(fields[0]) != "label" ||
        lower(fields[1]) != "phrase") {
        return fail(1, "must be the header label,phrase");
    }
    size_t row = 1, added = 0;
    while (!r.done()) {
        ++row;
        if (!r.next(fields, error)) return fail(row, error);
        if (fields.size() != 2) {
            return fail(row, "has " + std::to_string(fields.size()) + (fields.size() == 1 ? " column" : " columns") +
                                 ", not 2 (label,phrase)");
        }
        const std::string label = lf(fields[0]), text = lf(fields[1]);
        const std::string why = refused(label, text);
        if (!why.empty()) return fail(row, why);
        if (next.size() >= phrases::kMaxButtons) {
            return fail(row, "makes more than " + std::to_string(phrases::kMaxButtons) + " Buttons");
        }
        if (!next.add(label, text)) return fail(row, "can't be added");
        ++added;
    }
    if (added == 0) {
        error = "The file has no Buttons, only the header.";
        return false;
    }
    out = std::move(next);
    return true;
}

}  // namespace web
