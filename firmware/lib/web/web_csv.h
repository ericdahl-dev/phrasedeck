// The web editor's CSV file (#164): the Buttons exported for a spreadsheet, and a file from one
// imported back, all or nothing. Host-tested; no Arduino, httpd or LVGL.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "phrases.h"

namespace web {

// The most a POST /import body may be: a full list with every byte quoted, plus the form's parts.
constexpr size_t kMaxImportBody = 200 * 1024;

// The export's headers (#168). It opens in the tab as text, not as a download: Chrome won't
// download from a site with a self-signed certificate, even after the user clicks past the warning.
// The file name still applies when the user saves the page.
constexpr const char* kCsvContentType = "text/plain; charset=utf-8";
constexpr const char* kCsvDisposition = "inline; filename=\"phrasedeck-buttons.csv\"";

// The export, sent in parts: csvHead() once, then csvRow() for each Button in panel order.
// RFC 4180 with CRLF between rows; a field with a comma, quote or line break is quoted. The head
// starts with a UTF-8 BOM, which Excel needs to read the file as UTF-8. Columns: label,phrase,enter,
// enter being yes or no (Send Enter, #171).
std::string csvHead();
std::string csvRow(const phrases::Button& b);

// One import from the page: which button was pressed, and the file. csv points into the body.
struct Import {
    enum class Mode { Replace, Append } mode = Mode::Append;
    std::string_view csv;
};

// Parses a multipart/form-data body (do=replace|append, file). False if malformed.
bool importFrom(const std::string& contentType, std::string_view body, Import& im);

// The list after the import, in out: Replace makes the file the whole list, in its order; Append
// adds its rows after the Buttons. A file without the enter column (from before #171) imports with
// Send Enter off; in one with it, yes or no in any case, and an empty cell is no. Every imported Button gets a new id (the list's id counter goes
// on) and must be confirmed on the panel before its first Send. False if any row is malformed or
// over the limits, and error then names the row. list itself never changes, so the caller can keep
// it if saving fails, and only two lists are ever in memory.
bool apply(const phrases::List& list, const Import& im, phrases::List& out, std::string& error);

}  // namespace web
