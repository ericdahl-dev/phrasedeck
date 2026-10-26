// The panel's HTTPS server (#98): its own ECDSA key and self-signed cert, made on the panel the
// first time, and for now the admin login. Runs on its own task; never calls lv_*.
#pragma once

#include <string>

namespace web_server {

// Starts a task that waits for Wi-Fi, loads or makes the key and cert, then serves on 443.
bool start();

// The cert's SHA-256 as AA:BB:..., what a browser shows; empty until the cert is ready.
std::string fingerprint();

}  // namespace web_server
