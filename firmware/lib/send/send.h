// A Send: one tap's attempt to type a Button's Phrase on a Computer (CONTEXT.md).
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

namespace send {

enum class Outcome { Typed, NotTyped, MaybeTyped };

// sent: the first byte of the request was written (TLS up and the Pin matched); a write that fails
// partway still counts. status: /type's HTTP status, or 0 or below when no reply came back. 500 means typing failed partway; every other error means nothing was
// typed. With no reply, only a request that went out may have typed something.
inline Outcome outcome(bool sent, int status) {
    if (status == 200) return Outcome::Typed;
    if (status == 500 || (sent && status <= 0)) return Outcome::MaybeTyped;
    return Outcome::NotTyped;
}

// /type's JSON body. phrase and edit let the Helper notice the first Send of an edited Phrase
// (#98); an empty phrase leaves both out.
inline std::string typeBody(const std::string& text, const std::string& phrase, uint32_t edit) {
    JsonDocument doc;
    doc["text"] = text;
    if (!phrase.empty()) {
        doc["phrase"] = phrase;
        doc["edit"] = edit;
    }
    std::string json;
    serializeJson(doc, json);
    return json;
}

}  // namespace send
