#include <unity.h>

#include <string>

#include "phrases.h"
#include "web_editor.h"

void setUp() {}
void tearDown() {}

using web::Edit;

phrases::List twoButtons() {
    phrases::List l;
    l.add("Hello", "Hello there", false);
    l.add("Bye", "Goodbye", false);
    return l;
}

// The page shows labels and Phrases as text, never as markup (XSS, #98 AppSec).
void test_labels_and_phrases_are_escaped_on_the_page() {
    phrases::List l;
    l.add("<b>x</b>", "a\"b'c&d<script>alert(1)</script>", false);
    const std::string page = web::editorPage(l);
    TEST_ASSERT_TRUE(page.find("<script>") == std::string::npos);
    TEST_ASSERT_TRUE(page.find("<b>x</b>") == std::string::npos);
    TEST_ASSERT_TRUE(page.find("&lt;b&gt;x&lt;/b&gt;") != std::string::npos);
    TEST_ASSERT_TRUE(page.find("a&quot;b&#39;c&amp;d&lt;script&gt;") != std::string::npos);
}

// Eric (#98): the editor shows each Enter as ↵.
void test_the_page_shows_enter_as_a_mark() {
    phrases::List l;
    l.add("Sig", "Thanks\nEric", false);
    TEST_ASSERT_TRUE(web::editorPage(l).find("Thanks\xE2\x86\xB5\nEric") != std::string::npos);
}

// Each form names the Button by id and edit, so a stale page can't change the wrong one.
void test_a_change_form_parses_to_an_edit() {
    Edit e;
    TEST_ASSERT_TRUE(web::editFrom("do=change&id=2&edit=1&label=Bye%21&text=See+you%0D%0Asoon", e));
    TEST_ASSERT_EQUAL(static_cast<int>(Edit::Kind::Change), static_cast<int>(e.kind));
    TEST_ASSERT_EQUAL_UINT32(2, e.id);
    TEST_ASSERT_EQUAL_UINT32(1, e.edit);
    TEST_ASSERT_EQUAL_STRING("Bye!", e.label.c_str());
    TEST_ASSERT_EQUAL_STRING("See you\nsoon", e.text.c_str());  // a browser sends CRLF; the panel types LF
    TEST_ASSERT_TRUE(web::editFrom("do=add&label=A&text=B", e));
    TEST_ASSERT_EQUAL(static_cast<int>(Edit::Kind::Add), static_cast<int>(e.kind));
    TEST_ASSERT_FALSE(web::editFrom("do=explode&id=1&edit=1", e));
    TEST_ASSERT_FALSE(web::editFrom("do=remove&id=x&edit=1", e));
    TEST_ASSERT_FALSE(web::editFrom("do=remove&id=1", e));
}

void test_edits_apply_to_the_button_they_name() {
    phrases::List l = twoButtons();
    const uint32_t bye = l.at(1).id;
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Change, bye, 1, "Later", "See you"}));
    TEST_ASSERT_EQUAL_STRING("See you", l.at(1).text.c_str());
    TEST_ASSERT_TRUE(l.needsConfirm(1));  // changed on the web: confirmed on the panel before it sends
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Up, bye, 2, "", ""}));
    TEST_ASSERT_EQUAL_UINT32(bye, l.at(0).id);
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Down, bye, 2, "", ""}));
    TEST_ASSERT_EQUAL_UINT32(bye, l.at(1).id);
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Add, 0, 0, "New", "Phrase"}));
    TEST_ASSERT_EQUAL(3, l.size());
    TEST_ASSERT_TRUE(l.needsConfirm(2));
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Remove, bye, 2, "", ""}));
    TEST_ASSERT_EQUAL(2, l.size());
}

// A page opened before an edit on the panel (or another tab) changes nothing.
void test_a_stale_edit_changes_nothing() {
    phrases::List l = twoButtons();
    const uint32_t bye = l.at(1).id;
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Change, bye, 1, "Later", "See you"}));
    TEST_ASSERT_FALSE(web::apply(l, {Edit::Kind::Remove, bye, 1, "", ""}));
    TEST_ASSERT_FALSE(web::apply(l, {Edit::Kind::Change, 999, 1, "X", "Y"}));
    TEST_ASSERT_FALSE(web::apply(l, {Edit::Kind::Up, l.at(0).id, 1, "", ""}));  // already first
    TEST_ASSERT_EQUAL(2, l.size());
}

// #164: a link to the Buttons as CSV, and a plain form (no JavaScript) to import a file. The link
// opens the CSV in the tab (#168): Chrome won't download from a site with a self-signed certificate.
void test_the_page_links_the_csv_and_has_an_import_form() {
    const std::string page = web::editorPage(twoButtons());
    TEST_ASSERT_TRUE(page.find("<a href=/buttons.csv>View CSV</a> (save it with Ctrl+S or Cmd+S)") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(page.find(" download") == std::string::npos);
    TEST_ASSERT_TRUE(page.find("<form method=post action=/import enctype=multipart/form-data>") != std::string::npos);
    TEST_ASSERT_TRUE(page.find("<input type=file name=file") != std::string::npos);
    // Append first: Enter in the form submits the first button, and that mustn't replace everything.
    TEST_ASSERT_TRUE(page.find("<button name=do value=append>Append</button> "
                               "<button name=do value=replace>Replace all</button>") != std::string::npos);
}

// Send Enter (#171): a checkbox when adding or changing a Button, sent as enter=yes when ticked and
// left out of the form when not, so it's off unless asked for.
void test_the_send_enter_checkbox_parses_and_applies() {
    Edit e;
    TEST_ASSERT_TRUE(web::editFrom("do=add&label=Go&text=submit&enter=yes", e));
    TEST_ASSERT_TRUE(e.enter);
    TEST_ASSERT_TRUE(web::editFrom("do=change&id=2&edit=1&label=Go&text=submit", e));
    TEST_ASSERT_FALSE(e.enter);

    phrases::List l = twoButtons();
    const uint32_t bye = l.at(1).id;
    l.confirmed(1);
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Change, bye, 1, "Bye", "Goodbye", true}));
    TEST_ASSERT_TRUE(l.at(1).enter);
    TEST_ASSERT_TRUE(l.needsConfirm(1));  // turning it on is an edit, confirmed on the panel
    TEST_ASSERT_TRUE(web::apply(l, {Edit::Kind::Add, 0, 0, "New", "Phrase", true}));
    TEST_ASSERT_TRUE(l.at(2).enter);
}

// The list shows which Buttons press Enter, and each form's checkbox starts as the Button has it.
void test_the_page_shows_which_buttons_send_enter() {
    phrases::List l;
    l.add("Plain", "no Enter", false);
    l.add("Go", "submit", true);
    TEST_ASSERT_TRUE(web::editorButton(l.at(0)).find("<summary>Plain</summary>") != std::string::npos);
    TEST_ASSERT_TRUE(web::editorButton(l.at(0)).find("<input type=checkbox name=enter value=yes>") != std::string::npos);
    TEST_ASSERT_TRUE(web::editorButton(l.at(1)).find("<summary>Go (Send Enter)</summary>") != std::string::npos);
    TEST_ASSERT_TRUE(web::editorButton(l.at(1)).find("<input type=checkbox name=enter value=yes checked>") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(web::editorTail().find("<input type=checkbox name=enter value=yes>") != std::string::npos);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_send_enter_checkbox_parses_and_applies);
    RUN_TEST(test_the_page_shows_which_buttons_send_enter);
    RUN_TEST(test_the_page_links_the_csv_and_has_an_import_form);
    RUN_TEST(test_labels_and_phrases_are_escaped_on_the_page);
    RUN_TEST(test_the_page_shows_enter_as_a_mark);
    RUN_TEST(test_a_change_form_parses_to_an_edit);
    RUN_TEST(test_edits_apply_to_the_button_they_name);
    RUN_TEST(test_a_stale_edit_changes_nothing);
    return UNITY_END();
}
