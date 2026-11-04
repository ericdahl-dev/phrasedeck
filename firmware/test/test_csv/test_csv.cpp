#include <unity.h>

#include <string>

#include "phrases.h"
#include "web_csv.h"

void setUp() {}
void tearDown() {}

using web::Import;

// What the LVGL loop does: the result replaces the list only if the import is accepted.
bool importInto(phrases::List& l, const Import& im, std::string& error) {
    phrases::List out;
    if (!web::apply(l, im, out, error)) return false;
    l = std::move(out);
    return true;
}

std::string exported(const phrases::List& l) {
    std::string csv = web::csvHead();
    for (size_t i = 0; i < l.size(); ++i) csv += web::csvRow(l.at(i));
    return csv;
}

// Eric (#164): export, edit in a spreadsheet, import back. Nothing a Phrase can hold is lost.
void test_export_then_import_gives_back_the_same_buttons() {
    phrases::List l;
    l.add("Plain", "Hello there", false);
    l.add("Comma, \"quote\"", "a,b \"c\" d", false);
    l.add("Sig", "Thanks\nEric\n", false);
    l.add("Tab", "col1\tcol2", false);
    l.add("Caf\xC3\xA9", "\xE2\x9C\x93 done, na\xC3\xAFve", false);
    l.add("Go", "submit me", true);
    const std::string csv = exported(l);
    phrases::List back;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(importInto(back, {Import::Mode::Replace, csv}, error), error.c_str());
    TEST_ASSERT_EQUAL(l.size(), back.size());
    for (size_t i = 0; i < l.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(l.at(i).label.c_str(), back.at(i).label.c_str());
        TEST_ASSERT_EQUAL_STRING(l.at(i).text.c_str(), back.at(i).text.c_str());
        TEST_ASSERT_EQUAL(l.at(i).enter, back.at(i).enter);
    }
}

// Send Enter (#171): a third column, enter, yes or no (any case; an empty cell is no). A file from
// before it, label,phrase only, still imports with Send Enter off. Any other value is refused.
std::string importedEnters(const std::string& csv) {
    phrases::List l;
    std::string error;
    if (!importInto(l, {Import::Mode::Replace, csv}, error)) return "refused: " + error;
    std::string out;
    for (size_t i = 0; i < l.size(); ++i) out += l.at(i).label + (l.at(i).enter ? "=yes;" : "=no;");
    return out;
}

void test_the_enter_column_imports_and_old_files_still_do() {
    TEST_ASSERT_EQUAL_STRING("A=yes;B=no;C=yes;D=no;E=no;",
                             importedEnters("label,phrase,enter\nA,1,yes\nB,2,no\nC,3,YES\nD,4,No\nE,5,\n").c_str());
    TEST_ASSERT_EQUAL_STRING("A=no;B=no;", importedEnters("label,phrase\r\nA,1\r\nB,2\r\n").c_str());
    TEST_ASSERT_EQUAL_STRING("A=yes;", importedEnters("Label,Phrase,Enter\nA,1,yes").c_str());
}

// RFC 4180: CRLF between rows, and a field with a comma, quote or line break in quotes. A UTF-8
// BOM first, or Excel opens the file as Windows-1252 and garbles every é.
void test_the_export_is_rfc_4180() {
    phrases::List l;
    l.add("A", "plain", false);
    l.add("B, c", "say \"hi\"\nthen go", false);
    l.add("C", "submit", true);
    TEST_ASSERT_EQUAL_STRING("\xEF\xBB\xBFlabel,phrase,enter\r\nA,plain,no\r\n\"B, c\",\"say \"\"hi\"\"\nthen go\",no\r\nC,submit,yes\r\n",
                             exported(l).c_str());
}

std::string importedTexts(const std::string& csv) {
    phrases::List l;
    std::string error;
    if (!importInto(l, {Import::Mode::Replace, csv}, error)) return "refused: " + error;
    std::string out;
    for (size_t i = 0; i < l.size(); ++i) out += l.at(i).label + "=" + l.at(i).text + ";";
    return out;
}

// What spreadsheets write: Excel's BOM, LF or CRLF, no final line break, blank lines at the end.
void test_the_import_takes_any_line_ending_and_a_bom() {
    TEST_ASSERT_EQUAL_STRING("A=1;B=2;", importedTexts("label,phrase\nA,1\nB,2\n").c_str());
    TEST_ASSERT_EQUAL_STRING("A=1;B=2;", importedTexts("label,phrase\r\nA,1\r\nB,2\r\n").c_str());
    TEST_ASSERT_EQUAL_STRING("A=1;B=2;", importedTexts("\xEF\xBB\xBFlabel,phrase\r\nA,1\r\nB,2").c_str());
    TEST_ASSERT_EQUAL_STRING("A=1;B=2;", importedTexts("Label,Phrase\nA,1\nB,2\n\n\r\n").c_str());
    TEST_ASSERT_EQUAL_STRING("A=1;", importedTexts("\"LABEL\",\"phrase\"\nA,\"1\"").c_str());
}

// A spreadsheet's Enter in a cell may come as CRLF; the panel types LF, as the form does.
void test_a_crlf_inside_a_phrase_becomes_lf() {
    TEST_ASSERT_EQUAL_STRING("Sig=Thanks\nEric;", importedTexts("label,phrase\r\nSig,\"Thanks\r\nEric\"\r\n").c_str());
}

phrases::List twoButtons() {
    phrases::List l;
    l.add("Hello", "Hello there", false);
    l.add("Bye", "Goodbye", false);
    return l;
}

// All or nothing (#164): a refused file leaves the list exactly as it was, and says which row.
void refusedAt(const std::string& csv, const char* row, Import::Mode mode = Import::Mode::Replace) {
    phrases::List l = twoButtons();
    const std::string before = l.toJson();
    std::string error;
    TEST_ASSERT_FALSE_MESSAGE(importInto(l, {mode, csv}, error), csv.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), l.toJson().c_str());
    // "Row 2" must be the row the error starts with, not the start of "Row 20".
    const std::string want(row);
    const bool found = want.rfind("Row ", 0) == 0 ? error.rfind(want + " ", 0) == 0 : error.find(want) != std::string::npos;
    TEST_ASSERT_TRUE_MESSAGE(found, error.c_str());
}

void test_a_file_without_the_header_changes_nothing() {
    refusedAt("", "Row 1");
    refusedAt("A,1\nB,2\n", "Row 1");
    refusedAt("label,text\nA,1\n", "Row 1");
    refusedAt("label,phrase,color\nA,1,red\n", "Row 1");
}

void test_a_bad_enter_value_changes_nothing() {
    refusedAt("label,phrase,enter\nA,1,yes\nB,2,maybe\n", "Row 3");
    refusedAt("label,phrase,enter\nA,1,true\n", "Row 2");
    refusedAt("label,phrase,enter\nA,1\n", "Row 2");      // a column short
    refusedAt("label,phrase\nA,1,yes\n", "Row 2");        // more columns than the header
    refusedAt("label,phrase,send\nA,1,yes\n", "Row 1");
}

void test_a_malformed_row_changes_nothing() {
    refusedAt("label,phrase\nA,1\nB,\"unclosed\n", "Row 3");
    refusedAt("label,phrase\nA,1\nB,2,3\n", "Row 3");
    refusedAt("label,phrase\nA,1\nB\n", "Row 3");
    refusedAt("label,phrase\nA,\"1\"x\n", "Row 2");
    refusedAt("label,phrase\nA,1\"x\n", "Row 2");
    refusedAt("label,phrase\nA,\xC3\n", "Row 2");  // not UTF-8 (a Latin-1 export)
}

void test_a_row_over_the_limits_changes_nothing() {
    refusedAt("label,phrase\nA,1\n,2\n", "Row 3");
    refusedAt("label,phrase\nA,1\nB,\n", "Row 3");
    refusedAt("label,phrase\n" + std::string(41, 'x') + ",1\n", "Row 2");
    refusedAt("label,phrase\nA," + std::string(2001, 'x') + "\n", "Row 2");
    refusedAt("label,phrase\n\"A\nB\",1\n", "Row 2");  // an Enter in a label
    refusedAt("label,phrase\nA,\"1\x01\"\n", "Row 2");
    refusedAt("label,phrase\n", "header");  // no Buttons at all
    refusedAt("label,phrase\n,\n\n\"\",\"\"\n", "header");  // only empty rows
}

std::string rows(size_t n) {
    std::string csv = "label,phrase\n";
    for (size_t i = 0; i < n; ++i) csv += "B" + std::to_string(i) + ",P\n";
    return csv;
}

void test_more_than_48_buttons_changes_nothing() {
    refusedAt(rows(49), "Row 50");
    refusedAt(rows(47), "Row 48", Import::Mode::Append);  // 2 + 47
    phrases::List l = twoButtons();
    std::string error;
    TEST_ASSERT_TRUE(importInto(l, {Import::Mode::Append, rows(46)}, error));
    TEST_ASSERT_EQUAL(48, l.size());
}

// Replace: the file is the whole list, in its order, with new ids, each to be confirmed on the panel.
void test_replace_makes_the_file_the_list_with_new_ids() {
    phrases::List l = twoButtons();
    l.confirmed(0);
    const uint32_t oldIds = l.at(1).id;
    std::string error;
    TEST_ASSERT_TRUE(importInto(l, {Import::Mode::Replace, "label,phrase\nHello,Hello there\nNew,One\n"}, error));
    TEST_ASSERT_EQUAL(2, l.size());
    TEST_ASSERT_EQUAL_STRING("Hello", l.at(0).label.c_str());
    TEST_ASSERT_EQUAL_STRING("New", l.at(1).label.c_str());
    TEST_ASSERT_TRUE(l.at(0).id > oldIds);  // ids are never reused, even for the same Phrase
    TEST_ASSERT_TRUE(l.at(1).id > l.at(0).id);
    TEST_ASSERT_TRUE(l.needsConfirm(0));
    TEST_ASSERT_TRUE(l.needsConfirm(1));
}

// Append: the rows go after the Buttons there, which keep their ids and confirmations.
void test_append_adds_the_rows_after_the_buttons() {
    phrases::List l = twoButtons();
    l.confirmed(0);
    const uint32_t hello = l.at(0).id;
    std::string error;
    TEST_ASSERT_TRUE(importInto(l, {Import::Mode::Append, "label,phrase\nC,3\nD,4\n"}, error));
    TEST_ASSERT_EQUAL(4, l.size());
    TEST_ASSERT_EQUAL_UINT32(hello, l.at(0).id);
    TEST_ASSERT_FALSE(l.needsConfirm(0));
    TEST_ASSERT_EQUAL_STRING("C", l.at(2).label.c_str());
    TEST_ASSERT_EQUAL_STRING("D", l.at(3).label.c_str());
    TEST_ASSERT_TRUE(l.needsConfirm(2));
}

// Excel saves rows it thinks are used as ",", and people leave blank lines: both are skipped
// anywhere, and a row number still counts them, as the spreadsheet does.
void test_blank_and_empty_rows_are_skipped_anywhere() {
    TEST_ASSERT_EQUAL_STRING("A=1;B=2;", importedTexts("label,phrase\n\nA,1\n,\n\r\nB,2\n,\n,\n").c_str());
    TEST_ASSERT_EQUAL_STRING("A=1;B=2;", importedTexts("label,phrase\r\nA,1\r\n\"\",\"\"\r\nB,2\r\n").c_str());
    refusedAt("label,phrase\nA,1\n,\nB,\n", "Row 4");
}

const std::string kType ="multipart/form-data; boundary=----WebKitFormBoundaryAbc123";

std::string part(const std::string& disposition, const std::string& data) {
    return "------WebKitFormBoundaryAbc123\r\nContent-Disposition: form-data; " + disposition + "\r\n" +
           (disposition.find("filename") != std::string::npos ? "Content-Type: text/csv\r\n" : "") + "\r\n" + data + "\r\n";
}
const std::string kEnd = "------WebKitFormBoundaryAbc123--\r\n";

// The page has no JavaScript (#164): the file comes as a plain multipart/form-data form POST.
void test_an_import_form_parses_to_an_import() {
    const std::string csv = "label,phrase\r\nA,\"x\r\n--y\"\r\n";
    const std::string body = part("name=\"file\"; filename=\"name=do.csv\"", csv) + part("name=\"do\"", "append") + kEnd;
    Import im;
    TEST_ASSERT_TRUE(web::importFrom(kType, body, im));
    TEST_ASSERT_EQUAL(static_cast<int>(Import::Mode::Append), static_cast<int>(im.mode));
    TEST_ASSERT_EQUAL_STRING(csv.c_str(), std::string(im.csv).c_str());
    TEST_ASSERT_TRUE(web::importFrom("multipart/form-data; boundary=\"----WebKitFormBoundaryAbc123\"",
                                     part("name=\"do\"", "replace") + part("name=\"file\"; filename=\"b.csv\"", csv) + kEnd, im));
    TEST_ASSERT_EQUAL(static_cast<int>(Import::Mode::Replace), static_cast<int>(im.mode));
    TEST_ASSERT_EQUAL_STRING(csv.c_str(), std::string(im.csv).c_str());
}

// The body cap must take any list the panel can export: 48 Buttons of nothing but quotes, each
// doubled in the CSV, wrapped in the form as a browser sends it. And it imports back whole.
void test_the_largest_export_fits_the_import_cap() {
    phrases::List l;
    for (size_t i = 0; i < phrases::kMaxButtons; ++i) {
        TEST_ASSERT_TRUE(l.add(std::string(phrases::kMaxLabel, '"'), std::string(phrases::kMaxText, '"'), true));
    }
    const std::string body = part("name=\"file\"; filename=\"phrasedeck-buttons.csv\"", exported(l)) +
                             part("name=\"do\"", "replace") + kEnd;
    TEST_ASSERT_LESS_OR_EQUAL(web::kMaxImportBody, body.size());
    Import im;
    TEST_ASSERT_TRUE(web::importFrom(kType, body, im));
    phrases::List back;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(importInto(back, im, error), error.c_str());
    TEST_ASSERT_EQUAL(phrases::kMaxButtons, back.size());
    TEST_ASSERT_EQUAL_STRING(l.at(47).text.c_str(), back.at(47).text.c_str());
}

void test_a_malformed_import_form_is_refused() {
    const std::string file = part("name=\"file\"; filename=\"b.csv\"", "label,phrase\r\nA,1\r\n");
    Import im;
    TEST_ASSERT_FALSE(web::importFrom(kType, file + kEnd, im));                                   // no do
    TEST_ASSERT_FALSE(web::importFrom(kType, part("name=\"do\"", "replace") + kEnd, im));         // no file
    TEST_ASSERT_FALSE(web::importFrom(kType, file + part("name=\"do\"", "explode") + kEnd, im));  // unknown do
    TEST_ASSERT_FALSE(web::importFrom(kType, file + part("name=\"do\"", "append"), im));          // no closing boundary
    TEST_ASSERT_FALSE(web::importFrom("application/x-www-form-urlencoded", file + part("name=\"do\"", "append") + kEnd, im));
    TEST_ASSERT_FALSE(web::importFrom("multipart/form-data", file + part("name=\"do\"", "append") + kEnd, im));
    TEST_ASSERT_FALSE(web::importFrom(kType, "", im));
}

// #168: the export opens in the tab as text, since Chrome won't download over a self-signed
// certificate. The name still applies when the user saves it.
void test_the_export_opens_in_the_tab_as_text() {
    TEST_ASSERT_EQUAL_STRING("text/plain; charset=utf-8", web::kCsvContentType);
    TEST_ASSERT_EQUAL_STRING("inline; filename=\"phrasedeck-buttons.csv\"", web::kCsvDisposition);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_export_opens_in_the_tab_as_text);
    RUN_TEST(test_blank_and_empty_rows_are_skipped_anywhere);
    RUN_TEST(test_an_import_form_parses_to_an_import);
    RUN_TEST(test_the_largest_export_fits_the_import_cap);
    RUN_TEST(test_a_malformed_import_form_is_refused);
    RUN_TEST(test_a_file_without_the_header_changes_nothing);
    RUN_TEST(test_a_malformed_row_changes_nothing);
    RUN_TEST(test_the_enter_column_imports_and_old_files_still_do);
    RUN_TEST(test_a_bad_enter_value_changes_nothing);
    RUN_TEST(test_a_row_over_the_limits_changes_nothing);
    RUN_TEST(test_more_than_48_buttons_changes_nothing);
    RUN_TEST(test_replace_makes_the_file_the_list_with_new_ids);
    RUN_TEST(test_append_adds_the_rows_after_the_buttons);
    RUN_TEST(test_export_then_import_gives_back_the_same_buttons);
    RUN_TEST(test_the_export_is_rfc_4180);
    RUN_TEST(test_the_import_takes_any_line_ending_and_a_bom);
    RUN_TEST(test_a_crlf_inside_a_phrase_becomes_lf);
    return UNITY_END();
}
