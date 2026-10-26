#include <unity.h>

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
    TEST_ASSERT_FALSE(list.fromJson("{\"buttons\":[{\"label\":\"\",\"text\":\"x\"}]}"));
    std::string many = "{\"buttons\":[";
    for (size_t i = 0; i <= phrases::kMaxButtons; ++i) many += std::string(i ? "," : "") + "{\"label\":\"b\",\"text\":\"p\"}";
    TEST_ASSERT_FALSE(list.fromJson(many + "]}"));
    TEST_ASSERT_EQUAL(1, list.size());
    TEST_ASSERT_EQUAL_STRING("Keep", list.at(0).label.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_buttons_can_be_added_edited_moved_and_deleted);
    RUN_TEST(test_count_and_lengths_are_capped);
    RUN_TEST(test_the_list_round_trips_through_json);
    RUN_TEST(test_a_bad_file_is_refused_and_the_list_kept);
    return UNITY_END();
}
