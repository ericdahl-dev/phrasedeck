// The panel's HTTPS server (#98): its own ECDSA key and self-signed cert, made on the panel the
// first time, the admin login and the Button editor. Runs on its own task; never calls lv_*.
#pragma once

#include <cstdint>
#include <string>

#include "web_editor.h"

namespace web_server {

// Starts a task that waits for Wi-Fi, loads or makes the key and cert, then serves on 443.
bool start();

// The cert's SHA-256 as AA:BB:..., what a browser shows; empty until the cert is ready.
std::string fingerprint();

// The LVGL loop's side of the editor; the httpd task never touches the Buttons itself.
// publish: the list as phrases::List::toJson, after every load and save, for the page to show.
void publish(const std::string& json);
// takeEdit: one change waiting from the page, if any; the loop applies it, then calls editDone.
bool takeEdit(web::Edit& e, uint32_t& seq);
void editDone(uint32_t seq, bool ok);

}  // namespace web_server
