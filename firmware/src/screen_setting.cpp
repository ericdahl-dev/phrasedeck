#include "screen_setting.h"

#include <Preferences.h>

#include "panel_store.h"
#include "screen_rotation.h"

namespace screen_setting {

namespace {

constexpr const char* kNamespace = "screen";
constexpr const char* kKey = "rot180";

screen_rotation::Store store() {
    return {[] {
                Preferences p;
                // Read-only: a namespace that was never written doesn't open, which means "none".
                if (!p.begin(kNamespace, true, panel_store::kPartition)) return -1;
                const int v = p.isKey(kKey) ? p.getUChar(kKey, screen_rotation::kOff) : -1;
                p.end();
                return v;
            },
            [](uint8_t v) {
                Preferences p;
                if (!p.begin(kNamespace, false, panel_store::kPartition)) return false;
                const bool ok = p.putUChar(kKey, v) > 0;
                p.end();
                return ok;
            }};
}

}  // namespace

bool load() { return screen_rotation::load(store()); }

bool save(bool rotated) { return screen_rotation::save(store(), rotated); }

}  // namespace screen_setting
