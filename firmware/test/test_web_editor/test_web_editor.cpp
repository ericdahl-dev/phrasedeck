#include <unity.h>

#include <string>

#include "phrases.h"
#include "web_editor.h"

void setUp() {}
void tearDown() {}

using web::Edit;

phrases::List twoButtons() {
    phrases::List l;
    l.add("Hello", "Hello there");
    l.add("Bye", "Goodbye");
    return l;
}

// The page shows labels and Phrases as text, never as markup (XSS, #98 AppSec).
void test_labels_and_phrases_are_escaped_on_the_page() {
    phrases::List l;
    l.add("<b>x</b>", "a\"b'c&d<script>alert(1)</script>");
    const std::string page = web::editorPage(l);
    TEST_ASSERT_TRUE(page.find("<script>") == std::string::npos);
    TEST_ASSERT_TRUE(page.find("<b>x</b>") == std::string::npos);
    TEST_ASSERT_TRUE(page.find("&lt;b&gt;x&lt;/b&gt;") != std::string::npos);
    TEST_ASSERT_TRUE(page.find("a&quot;b&#39;c&amp;d&lt;script&gt;") != std::string::npos);
}

// Eric (#98): the editor shows each Enter as ↵.
void test_the_page_shows_enter_as_a_mark() {
    phrases::List l;
    l.add("Sig", "Thanks\nEric");
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_labels_and_phrases_are_escaped_on_the_page);
    RUN_TEST(test_the_page_shows_enter_as_a_mark);
    RUN_TEST(test_a_change_form_parses_to_an_edit);
    RUN_TEST(test_edits_apply_to_the_button_they_name);
    RUN_TEST(test_a_stale_edit_changes_nothing);
    return UNITY_END();
}
