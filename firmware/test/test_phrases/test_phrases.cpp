#include <unity.h>

#include <string>

#include "phrases.h"

void setUp() {}
void tearDown() {}

// The panel's own Phrase list (#98): add, edit, delete and reorder, saved as JSON.
void test_buttons_can_be_added_edited_moved_and_deleted() {
    phrases::List list;
    TEST_ASSERT_TRUE(list.add("LGTM", "Looks good to me"));
    TEST_ASSERT_TRUE(list.add("Late", "Running late"));
    TEST_ASSERT_TRUE(list.add("Ship", "Ship it"));
    TEST_ASSERT_TRUE(list.edit(1, "Late", "Running 5 min late"));
    TEST_ASSERT_TRUE(list.move(2, 0));  // Ship first
    TEST_ASSERT_TRUE(list.remove(1));   // LGTM gone
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_EQUAL_STRING("Ship", list.at(0).label.c_str());
    TEST_ASSERT_EQUAL_STRING("Running 5 min late", list.at(1).text.c_str());
    TEST_ASSERT_FALSE(list.remove(5));
    TEST_ASSERT_FALSE(list.move(0, 9));
    TEST_ASSERT_FALSE(list.edit(9, "x", "y"));
}

// Caps (#98): at most kMaxButtons, a label of 1 to kMaxLabel bytes, a Phrase of 1 to kMaxText
// bytes. A rejected add or edit changes nothing.
void test_count_and_lengths_are_capped() {
    phrases::List list;
    for (size_t i = 0; i < phrases::kMaxButtons; ++i) TEST_ASSERT_TRUE(list.add("b", "p"));
    TEST_ASSERT_FALSE(list.add("one", "too many"));
    TEST_ASSERT_EQUAL(phrases::kMaxButtons, list.size());

    phrases::List small;
    TEST_ASSERT_FALSE(small.add("", "text"));
    TEST_ASSERT_FALSE(small.add("label", ""));
    TEST_ASSERT_FALSE(small.add(std::string(phrases::kMaxLabel + 1, 'l'), "text"));
    TEST_ASSERT_FALSE(small.add("label", std::string(phrases::kMaxText + 1, 't')));
    TEST_ASSERT_TRUE(small.add(std::string(phrases::kMaxLabel, 'l'), std::string(phrases::kMaxText, 't')));
    TEST_ASSERT_FALSE(small.edit(0, "label", std::string(phrases::kMaxText + 1, 't')));
    TEST_ASSERT_EQUAL(phrases::kMaxText, small.at(0).text.size());
}

// The list round-trips through JSON (LittleFS on the panel), Unicode and quotes included.
void test_the_list_round_trips_through_json() {
    phrases::List list;
    list.add("Café", "naïve \"quoted\" ✓\nsecond line");
    list.add("Ship", "Ship it");
    phrases::List back;
    TEST_ASSERT_TRUE(back.fromJson(list.toJson()));
    TEST_ASSERT_EQUAL(2, back.size());
    TEST_ASSERT_EQUAL_STRING("Café", back.at(0).label.c_str());
    TEST_ASSERT_EQUAL_STRING("naïve \"quoted\" ✓\nsecond line", back.at(0).text.c_str());
}

// A damaged or hand-edited file never loads past the caps; it's refused whole, keeping the list.
void test_a_bad_file_is_refused_and_the_list_kept() {
    phrases::List list;
    list.add("Keep", "me");
    TEST_ASSERT_FALSE(list.fromJson("not json"));
    TEST_ASSERT_FALSE(list.fromJson("{\"buttons\":[{\"id\":1,\"edit\":1,\"label\":\"\",\"text\":\"x\"}]}"));
    TEST_ASSERT_FALSE(list.fromJson("{\"buttons\":[{\"label\":\"a\",\"text\":\"x\"}]}"));  // no id
    TEST_ASSERT_FALSE(list.fromJson("{\"buttons\":[{\"id\":1,\"edit\":1,\"label\":\"a\",\"text\":\"x\"},"
                                    "{\"id\":1,\"edit\":1,\"label\":\"b\",\"text\":\"y\"}]}"));  // same id twice
    std::string many = "{\"buttons\":[";
    for (size_t i = 0; i <= phrases::kMaxButtons; ++i) many += std::string(i ? "," : "") + "{\"id\":" + std::to_string(i + 1) + ",\"edit\":1,\"label\":\"b\",\"text\":\"p\"}";
    TEST_ASSERT_FALSE(list.fromJson(many + "]}"));
    TEST_ASSERT_EQUAL(1, list.size());
    TEST_ASSERT_EQUAL_STRING("Keep", list.at(0).label.c_str());
}

// Each Button has an id that never changes and an edit counter that goes up on every edit, so the
// Helper can notify the first time it types a changed Phrase (#98). Ids survive deletes and a
// save and load, and a deleted id is never reused.
void test_ids_stay_put_and_edits_count() {
    phrases::List list;
    list.add("A", "a");
    list.add("B", "b");
    const uint32_t idB = list.at(1).id;
    TEST_ASSERT_EQUAL_UINT32(1, list.at(1).edit);
    list.edit(1, "B", "b2");
    list.edit(1, "B", "b3");
    TEST_ASSERT_EQUAL_UINT32(3, list.at(1).edit);
    list.remove(0);
    list.add("C", "c");
    TEST_ASSERT_EQUAL_UINT32(idB, list.at(0).id);
    TEST_ASSERT_NOT_EQUAL(idB, list.at(1).id);
    phrases::List back;
    TEST_ASSERT_TRUE(back.fromJson(list.toJson()));
    TEST_ASSERT_EQUAL_UINT32(idB, back.at(0).id);
    TEST_ASSERT_EQUAL_UINT32(3, back.at(0).edit);
    back.add("D", "d");
    TEST_ASSERT_TRUE(back.at(2).id > list.at(1).id);
}

// Eric (#98): the first tap on a Phrase added or edited on the web page shows it and asks
// "Send?"; once confirmed, taps send at once until the next edit. The confirmation is saved.
void test_an_edited_phrase_needs_one_confirm() {
    phrases::List list;
    list.add("Ship", "Ship it");
    TEST_ASSERT_TRUE(list.needsConfirm(0));
    list.confirmed(0);
    TEST_ASSERT_FALSE(list.needsConfirm(0));
    phrases::List back;
    back.fromJson(list.toJson());
    TEST_ASSERT_FALSE(back.needsConfirm(0));
    back.edit(0, "Ship", "Ship it now");
    TEST_ASSERT_TRUE(back.needsConfirm(0));
    back.move(0, 0);
    TEST_ASSERT_TRUE(back.needsConfirm(0));
    TEST_ASSERT_TRUE(list.needsConfirm(9));  // out of range: never send without asking
}

// A saved count at its limit would wrap to 0 on the next add or edit: a reused id, or a web edit
// that skips the confirm. Such a file is refused; the counters stop below the limit.
void test_counters_never_wrap() {
    phrases::List list;
    const std::string top = std::to_string(phrases::kMaxCounter);
    TEST_ASSERT_FALSE(list.fromJson("{\"next\":0,\"buttons\":[]}"));
    TEST_ASSERT_FALSE(list.fromJson("{\"next\":" + std::to_string(phrases::kMaxCounter + 1u) + ",\"buttons\":[]}"));
    TEST_ASSERT_FALSE(list.fromJson("{\"buttons\":[{\"id\":" + top + ",\"edit\":1,\"label\":\"a\",\"text\":\"x\"}]}"));
    TEST_ASSERT_FALSE(list.fromJson("{\"buttons\":[{\"id\":1,\"edit\":" + std::to_string(phrases::kMaxCounter + 1u) + ",\"label\":\"a\",\"text\":\"x\"}]}"));
    TEST_ASSERT_EQUAL(0, list.size());

    TEST_ASSERT_TRUE(list.fromJson("{\"next\":" + top + ",\"buttons\":[{\"id\":1,\"edit\":" + top + ",\"label\":\"a\",\"text\":\"x\"}]}"));
    TEST_ASSERT_FALSE(list.add("b", "y"));
    TEST_ASSERT_FALSE(list.edit(0, "a", "z"));
    TEST_ASSERT_EQUAL_STRING("x", list.at(0).text.c_str());
}

// "Send?" confirms the edit it showed: if the Phrase changed, moved or went while it was open,
// the confirm finds nothing and nothing is sent.
void test_a_confirm_finds_only_the_edit_it_showed() {
    phrases::List list;
    list.add("A", "a");
    list.add("B", "b");
    const phrases::Button shown = list.at(1);
    TEST_ASSERT_EQUAL(1, list.find(shown.id, shown.edit));
    list.move(1, 0);
    TEST_ASSERT_EQUAL(0, list.find(shown.id, shown.edit));
    list.edit(0, "B", "b2");
    TEST_ASSERT_EQUAL(list.size(), list.find(shown.id, shown.edit));
    list.remove(0);
    TEST_ASSERT_EQUAL(list.size(), list.find(shown.id, shown.edit + 1));
}

// "Send?" shows what will be typed: Enter and Tab as visible marks, plus how many characters
// (not bytes) and lines it is.
void test_the_preview_shows_enter_and_tab_and_counts() {
    TEST_ASSERT_EQUAL_STRING("Hi<NL>\nthere<TAB>x", phrases::visible("Hi\nthere\tx", "<NL>", "<TAB>").c_str());
    TEST_ASSERT_EQUAL(7, phrases::characters("caf\xc3\xa9\n!!"));
    TEST_ASSERT_EQUAL(1, phrases::lines("one line"));
    TEST_ASSERT_EQUAL(3, phrases::lines("a\nb\nc"));
    TEST_ASSERT_EQUAL(2, phrases::lines("ends with Enter\n"));
}

// AppSec (#137): a NUL hides the rest of a Phrase from "Send?" (c_str stops there) while the Helper
// still types it; other control bytes draw as nothing. Only Enter and Tab are allowed.
void test_control_characters_are_refused() {
    phrases::List l;
    TEST_ASSERT_FALSE(l.add("Hi", std::string("echo hi\0rm -rf ~", 17)));
    TEST_ASSERT_FALSE(l.add("Hi", "esc\x1b[2J"));
    TEST_ASSERT_FALSE(l.add("Hi", "del\x7f"));
    TEST_ASSERT_FALSE(l.add("H\ni", "label with Enter"));
    TEST_ASSERT_FALSE(l.add(std::string("H\0i", 3), "x"));
    TEST_ASSERT_TRUE(l.add("Sig", "Line\nTab\there"));
    TEST_ASSERT_FALSE(l.edit(0, "Sig", "bell\a"));
    TEST_ASSERT_EQUAL_STRING("Line\nTab\there", l.at(0).text.c_str());
    phrases::List loaded;
    TEST_ASSERT_FALSE(loaded.fromJson("{\"next\":2,\"buttons\":[{\"id\":1,\"edit\":1,\"label\":\"x\",\"text\":\"a\\u001bb\"}]}"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_buttons_can_be_added_edited_moved_and_deleted);
    RUN_TEST(test_count_and_lengths_are_capped);
    RUN_TEST(test_the_list_round_trips_through_json);
    RUN_TEST(test_a_bad_file_is_refused_and_the_list_kept);
    RUN_TEST(test_ids_stay_put_and_edits_count);
    RUN_TEST(test_an_edited_phrase_needs_one_confirm);
    RUN_TEST(test_counters_never_wrap);
    RUN_TEST(test_a_confirm_finds_only_the_edit_it_showed);
    RUN_TEST(test_the_preview_shows_enter_and_tab_and_counts);
    RUN_TEST(test_control_characters_are_refused);
    return UNITY_END();
}
