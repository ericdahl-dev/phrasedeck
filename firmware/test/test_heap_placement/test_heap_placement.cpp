#include <unity.h>

#include <cstdlib>
#include <new>
#include <string>

#include "heap_placement.h"
#include "phrases.h"
#include "web_csv.h"

// Every new and delete in this test program goes through here, so a test can see how many bytes the
// Buttons hold that the panel would put in internal RAM. A header before each block keeps its size.
namespace {

bool g_tracking = false;
size_t g_internal = 0;      // live bytes a malloc() of the same size would put in internal RAM first
size_t g_internalPeak = 0;  // the most at once since tracking started

constexpr size_t kHeader = alignof(std::max_align_t);

void* tracked(size_t n) {
    void* raw = std::malloc(n + kHeader);
    if (raw == nullptr) throw std::bad_alloc();
    *static_cast<size_t*>(raw) = g_tracking ? n : 0;  // 0: not counted, so its delete isn't either
    if (g_tracking && heap_placement::internalFirst(n)) {
        g_internal += n;
        if (g_internal > g_internalPeak) g_internalPeak = g_internal;
    }
    return static_cast<char*>(raw) + kHeader;
}

void untracked(void* p) {
    if (p == nullptr) return;
    void* raw = static_cast<char*>(p) - kHeader;
    const size_t n = *static_cast<size_t*>(raw);
    if (n != 0 && heap_placement::internalFirst(n)) g_internal -= n;
    std::free(raw);
}

}  // namespace

void* operator new(size_t n) { return tracked(n); }
void* operator new[](size_t n) { return tracked(n); }
void operator delete(void* p) noexcept { untracked(p); }
void operator delete[](void* p) noexcept { untracked(p); }
void operator delete(void* p, size_t) noexcept { untracked(p); }
void operator delete[](void* p, size_t) noexcept { untracked(p); }

void setUp() {
    g_internal = g_internalPeak = 0;
    g_tracking = true;
}
void tearDown() { g_tracking = false; }

// firmware/tools/make_heap_csv.py: 48 Buttons, each with a 2000-byte Phrase.
std::string heapCsv() {
    std::string csv = "label,phrase,enter\r\n";
    for (size_t i = 0; i < phrases::kMaxButtons; ++i) {
        char label[16];
        snprintf(label, sizeof label, "Heap %02zu", i + 1);
        csv += label;
        csv += ',';
        csv += std::string(phrases::kMaxText, static_cast<char>('a' + i % 26));
        csv += ",no\r\n";
    }
    return csv;
}

phrases::List fullList() {
    phrases::List l;
    for (size_t i = 0; i < phrases::kMaxButtons; ++i) {
        l.add("Button " + std::to_string(i + 1), std::string(phrases::kMaxText, static_cast<char>('A' + i % 26)), false);
    }
    return l;
}

// What a Button's label can hold in internal RAM: 48 of the longest labels, in each of the two lists.
constexpr size_t kLabelsBudget = 2 * phrases::kMaxButtons * (phrases::kMaxLabel + 1);

// #182: the panel's own Buttons stay out of the internal RAM that Wi-Fi and TLS need.
void test_a_full_list_of_long_phrases_stays_out_of_internal_ram() {
    const phrases::List l = fullList();
    TEST_ASSERT_EQUAL(phrases::kMaxButtons, l.size());
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(kLabelsBudget, g_internal, "bytes held in internal RAM");
}

// A Replace with make_heap_csv.py over a full list: both lists are alive until it's saved (main.cpp
// applyWebImport, savePhrases), so the peak counts, not just what's left.
void test_replacing_a_full_list_with_another_stays_out_of_internal_ram() {
    phrases::List current = fullList();
    const std::string body = heapCsv();
    const web::Import im{web::Import::Mode::Replace, body};
    phrases::List next;
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(web::apply(current, im, next, error), error.c_str());
    TEST_ASSERT_EQUAL(phrases::kMaxButtons, next.size());
    std::swap(current, next);
    const std::string json = current.toJson();  // savePhrases writes and publishes this
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(kLabelsBudget, g_internalPeak, "peak bytes in internal RAM");
}

// After a restart, loadPhrases reads the saved list back before Wi-Fi and the web editor start.
void test_a_saved_full_list_loads_at_boot_out_of_internal_ram() {
    std::string json;
    {
        const phrases::List saved = fullList();
        json = saved.toJson();
    }
    g_internal = g_internalPeak = 0;
    phrases::List loaded;
    TEST_ASSERT_TRUE(loaded.fromJson(json));
    TEST_ASSERT_EQUAL(phrases::kMaxButtons, loaded.size());
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(kLabelsBudget, g_internal, "bytes held in internal RAM");
}

// The small allocations the panel makes all the time (a label, a short string) stay in fast
// internal RAM: the limit only moves what's big.
void test_small_allocations_stay_in_internal_ram() {
    TEST_ASSERT_TRUE(heap_placement::internalFirst(phrases::kMaxLabel + 1));
    TEST_ASSERT_TRUE(heap_placement::internalFirst(heap_placement::kInternalUpTo));
    TEST_ASSERT_FALSE(heap_placement::internalFirst(heap_placement::kInternalUpTo + 1));
}

// The web editor's TLS keeps the split it had (#94): a record buffer (16 KB, plus its header) in
// PSRAM, the small structures in internal RAM.
void test_tls_records_go_to_psram_and_its_small_structures_stay_internal() {
    TEST_ASSERT_FALSE(heap_placement::internalFirst(16384 + 29, heap_placement::kTlsInternalUpTo));
    TEST_ASSERT_TRUE(heap_placement::internalFirst(1024, heap_placement::kTlsInternalUpTo));
}

// The `heap` serial command (#182): free, largest block and least since boot, for each heap, so a
// before and after can be compared line by line.
void test_the_heap_report_shows_both_heaps() {
    const std::string r = heap_placement::report({63000, 31000, 52000}, {8000000, 7900000, 7800000});
    TEST_ASSERT_EQUAL_STRING(
        "heap: internal free 63000, largest 31000, min 52000; PSRAM free 8000000, largest 7900000, min 7800000",
        r.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_full_list_of_long_phrases_stays_out_of_internal_ram);
    RUN_TEST(test_replacing_a_full_list_with_another_stays_out_of_internal_ram);
    RUN_TEST(test_a_saved_full_list_loads_at_boot_out_of_internal_ram);
    RUN_TEST(test_small_allocations_stay_in_internal_ram);
    RUN_TEST(test_tls_records_go_to_psram_and_its_small_structures_stay_internal);
    RUN_TEST(test_the_heap_report_shows_both_heaps);
    return UNITY_END();
}
