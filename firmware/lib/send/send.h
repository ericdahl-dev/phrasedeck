// A Send: one tap's attempt to type a Button's Phrase on a Computer (CONTEXT.md).
#pragma once

namespace send {

enum class Outcome { Typed, NotTyped, MaybeTyped };

// sent: the request left the panel (TLS up and the Pin matched). status: /type's HTTP status, or
// 0 when no reply came back. 500 means typing failed partway; every other error means nothing was
// typed. With no reply, only a request that went out may have typed something.
inline Outcome outcome(bool sent, int status) {
    if (status == 200) return Outcome::Typed;
    if (status == 500 || (sent && status == 0)) return Outcome::MaybeTyped;
    return Outcome::NotTyped;
}

}  // namespace send
