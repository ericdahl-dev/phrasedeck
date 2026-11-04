#include <unity.h>

#include <functional>
#include <string>

#include "request_channel.h"

void setUp() {}
void tearDown() {}

using request_channel::Channel;
using request_channel::kBusy;
using request_channel::Request;
using request_channel::Ticket;

// The RTOS seam with no threads: the lock checks it's taken and given in pairs, never twice, and
// wait() reports whether a request was signaled since the last wait. duringWait stands in for the
// UI task submitting while the net task waits.
struct FakeRtos : request_channel::Rtos {
    int depth = 0, maxDepth = 0;
    bool signaled = false;
    unsigned waits = 0;
    uint32_t lastWaitMs = 0;
    std::function<void()> duringWait;
    void lock() override {
        ++depth;
        if (depth > maxDepth) maxDepth = depth;
    }
    void unlock() override { --depth; }
    void notify() override { signaled = true; }
    bool wait(uint32_t ms) override {
        ++waits;
        lastWaitMs = ms;
        TEST_ASSERT_EQUAL(0, depth);  // never waits holding the lock
        if (duringWait) {
            auto fn = duringWait;
            duringWait = nullptr;
            fn();
        }
        const bool was = signaled;
        signaled = false;
        return was;
    }
};

Request typeRequest(const std::string& text) {
    Request r;
    r.kind = Request::Type;
    r.phrase.text = text;
    return r;
}

// One request at a time: the channel refuses a second one while the first is in flight, whoever
// asks (a tap, the Pair screen or a dev serial command).
void test_a_second_request_while_one_is_in_flight_is_busy() {
    FakeRtos rtos;
    Channel ch;
    ch.begin(rtos);
    const Ticket t = ch.submit(typeRequest("hi"));
    TEST_ASSERT_NOT_EQUAL(kBusy, t);
    TEST_ASSERT_TRUE(ch.busy());
    TEST_ASSERT_EQUAL(kBusy, ch.submit(typeRequest("again")));
    TEST_ASSERT_EQUAL(0, rtos.depth);
    TEST_ASSERT_EQUAL(1, rtos.maxDepth);
}

// The net task waits on the seam for a request, runs it and finishes its ticket. Only that ticket
// reads the result; then the channel takes the next request.
void test_the_net_task_takes_the_request_and_its_ticket_gets_the_result() {
    FakeRtos rtos;
    Channel ch;
    ch.begin(rtos);
    request_channel::Taken taken;
    TEST_ASSERT_FALSE(ch.take(taken, 500));  // nothing yet: it waited on the seam
    TEST_ASSERT_EQUAL(1, rtos.waits);
    TEST_ASSERT_EQUAL(500, rtos.lastWaitMs);

    const Ticket t = ch.submit(typeRequest("Ship it"));
    TEST_ASSERT_TRUE(ch.take(taken, 500));
    TEST_ASSERT_EQUAL(t, taken.ticket);
    TEST_ASSERT_EQUAL(Request::Type, taken.request.kind);
    TEST_ASSERT_EQUAL_STRING("Ship it", taken.request.phrase.text.c_str());
    TEST_ASSERT_FALSE(ch.take(taken, 0));  // taken once

    request_channel::Result r;
    TEST_ASSERT_FALSE(ch.result(t, r));  // running
    TEST_ASSERT_TRUE(ch.busy());
    request_channel::Result done;
    done.message = "Typed";
    done.sent = send::Outcome::Typed;
    ch.finish(t, done);
    TEST_ASSERT_FALSE(ch.busy());
    TEST_ASSERT_TRUE(ch.result(t, r));
    TEST_ASSERT_EQUAL_STRING("Typed", r.message.c_str());
    TEST_ASSERT_EQUAL(send::Outcome::Typed, r.sent);
    TEST_ASSERT_FALSE(ch.result(t + 1, r));

    const Ticket next = ch.submit(typeRequest("again"));
    TEST_ASSERT_NOT_EQUAL(kBusy, next);
    TEST_ASSERT_NOT_EQUAL(t, next);
    TEST_ASSERT_FALSE(ch.result(next, r));
    TEST_ASSERT_EQUAL(0, rtos.depth);
    TEST_ASSERT_EQUAL(1, rtos.maxDepth);
}

// Stop by ticket: it reaches the Send the ticket names, also while it still waits to be taken (a
// browse holds the net task about 3 s). A Stopped Send still finishes with its result (Maybe typed).
void test_stop_reaches_the_send_its_ticket_names() {
    FakeRtos rtos;
    Channel ch;
    ch.begin(rtos);
    const Ticket t = ch.submit(typeRequest("a long one"));
    TEST_ASSERT_FALSE(ch.stop().load());
    ch.cancel(t);  // before the net task took it
    request_channel::Taken taken;
    TEST_ASSERT_TRUE(ch.take(taken, 0));
    TEST_ASSERT_TRUE(ch.stop().load());
    TEST_ASSERT_TRUE(ch.busy());  // Stop doesn't end the ticket: the Send does
    request_channel::Result r;
    r.sent = send::Outcome::MaybeTyped;
    ch.finish(t, r);
    TEST_ASSERT_TRUE(ch.result(t, r));
    TEST_ASSERT_EQUAL(send::Outcome::MaybeTyped, r.sent);
    TEST_ASSERT_EQUAL(0, rtos.depth);
}

// A Stop that comes late (its Send already finished, and the next one started) mustn't stop the next
// one; and the old Send's result never reads as the new ticket's.
void test_a_stale_stop_or_result_does_not_touch_the_next_send() {
    FakeRtos rtos;
    Channel ch;
    ch.begin(rtos);
    const Ticket first = ch.submit(typeRequest("one"));
    request_channel::Taken taken;
    ch.take(taken, 0);
    ch.cancel(first);
    request_channel::Result r;
    r.message = "Maybe typed";
    r.sent = send::Outcome::MaybeTyped;
    ch.finish(first, r);

    const Ticket second = ch.submit(typeRequest("two"));
    TEST_ASSERT_FALSE(ch.stop().load());  // a new Send starts unstopped
    ch.cancel(first);                     // late, for the old ticket
    TEST_ASSERT_FALSE(ch.stop().load());
    ch.finish(first, r);  // a stale finish for the old ticket: ignored
    TEST_ASSERT_TRUE(ch.busy());
    request_channel::Result got;
    TEST_ASSERT_FALSE(ch.result(second, got));
    TEST_ASSERT_TRUE(ch.result(first, got));  // the old ticket still reads its own
    TEST_ASSERT_EQUAL_STRING("Maybe typed", got.message.c_str());

    ch.cancel(second);
    TEST_ASSERT_TRUE(ch.stop().load());
    TEST_ASSERT_EQUAL(0, rtos.depth);
}

// One Snapshot, one version: the net task's network state and the Bluetooth keyboard's state each
// publish into it, and a finished request bumps it too, so the UI polls one number. The Bluetooth
// state is read under the lock, so a Snapshot is one moment.
void test_one_snapshot_carries_the_network_and_bluetooth_under_one_version() {
    FakeRtos rtos;
    Channel ch;
    ch.begin(rtos);
    const unsigned v0 = ch.version();

    request_channel::Network n;
    n.paired = "omarchy";
    n.wifiUp = true;
    n.ip = "192.168.1.20";
    ch.publish(n);
    const unsigned v1 = ch.version();
    TEST_ASSERT_TRUE(v1 != v0);

    bool readLocked = false;
    ch.updateBluetooth([&] {
        readLocked = rtos.depth == 1;
        request_channel::Bluetooth b;
        b.on = true;
        b.pairing = true;
        b.passkey = 123456;
        return b;
    });
    TEST_ASSERT_TRUE(readLocked);
    const unsigned v2 = ch.version();
    TEST_ASSERT_TRUE(v2 != v1);

    request_channel::Snapshot s = ch.snapshot();
    TEST_ASSERT_EQUAL(v2, s.version);
    TEST_ASSERT_EQUAL_STRING("omarchy", s.paired.c_str());  // the network part stayed
    TEST_ASSERT_EQUAL_STRING("192.168.1.20", s.ip.c_str());
    TEST_ASSERT_TRUE(s.bluetooth);
    TEST_ASSERT_TRUE(s.blePairing);
    TEST_ASSERT_FALSE(s.bleReady);
    TEST_ASSERT_EQUAL(123456, s.passkey);

    // The same Bluetooth state again: nothing to redraw.
    ch.updateBluetooth([] {
        request_channel::Bluetooth b;
        b.on = true;
        b.pairing = true;
        b.passkey = 123456;
        return b;
    });
    TEST_ASSERT_EQUAL(v2, ch.version());

    n.wifiUp = false;
    ch.publish(n);  // the Bluetooth part stays
    s = ch.snapshot();
    TEST_ASSERT_FALSE(s.wifiUp);
    TEST_ASSERT_TRUE(s.blePairing);

    // A finished request bumps the version, so the UI looks for its result.
    const Ticket t = ch.submit(typeRequest("hi"));
    const unsigned v3 = ch.version();
    ch.finish(t, request_channel::Result());
    TEST_ASSERT_TRUE(ch.version() != v3);
    TEST_ASSERT_EQUAL(ch.version(), ch.snapshot().version);
    TEST_ASSERT_EQUAL(0, rtos.depth);
    TEST_ASSERT_EQUAL(1, rtos.maxDepth);
}

// A request submitted while the net task waits wakes it, and the same take() returns it: it looks
// again after the wakeup instead of waiting for the next pass (up to 10 s between browses).
void test_a_request_submitted_during_the_wait_is_taken_by_that_take() {
    FakeRtos rtos;
    Channel ch;
    ch.begin(rtos);
    Ticket t = kBusy;
    rtos.duringWait = [&] { t = ch.submit(typeRequest("Ship it")); };
    request_channel::Taken taken;
    TEST_ASSERT_TRUE(ch.take(taken, 10000));
    TEST_ASSERT_NOT_EQUAL(kBusy, t);
    TEST_ASSERT_EQUAL(t, taken.ticket);
    TEST_ASSERT_EQUAL_STRING("Ship it", taken.request.phrase.text.c_str());
    TEST_ASSERT_EQUAL(1, rtos.waits);
    TEST_ASSERT_EQUAL(0, rtos.depth);
}

// Between browses the net task waits for requests until a deadline on the tick counter. A tick that
// passes the deadline between the check and the wait mustn't wrap into a wait of days, and the
// counter itself wraps.
void test_the_wait_until_a_deadline_never_wraps() {
    using request_channel::ticksLeft;
    TEST_ASSERT_EQUAL_UINT32(10000, ticksLeft(0, 10000));
    TEST_ASSERT_EQUAL_UINT32(1, ticksLeft(9999, 10000));
    TEST_ASSERT_EQUAL_UINT32(0, ticksLeft(10000, 10000));
    TEST_ASSERT_EQUAL_UINT32(0, ticksLeft(10001, 10000));  // passed it: no wait, not 4294967295
    TEST_ASSERT_EQUAL_UINT32(20, ticksLeft(0xFFFFFFF6u, 10));  // the counter wrapped before the deadline
    TEST_ASSERT_EQUAL_UINT32(0, ticksLeft(11, 0xFFFFFFF6u + 20));  // and after it
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_second_request_while_one_is_in_flight_is_busy);
    RUN_TEST(test_the_net_task_takes_the_request_and_its_ticket_gets_the_result);
    RUN_TEST(test_stop_reaches_the_send_its_ticket_names);
    RUN_TEST(test_a_stale_stop_or_result_does_not_touch_the_next_send);
    RUN_TEST(test_one_snapshot_carries_the_network_and_bluetooth_under_one_version);
    RUN_TEST(test_a_request_submitted_during_the_wait_is_taken_by_that_take);
    RUN_TEST(test_the_wait_until_a_deadline_never_wraps);
    return UNITY_END();
}
