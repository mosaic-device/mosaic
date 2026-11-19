#include "../policy.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

using namespace mosaic::features::connection;
using mosaic::events::Status;

namespace {
// Gives each backoff wait exactly: 1 s, 2 s, 4 s...
constexpr uint32_t kExact = 0x8000'0000;

// Wi-Fi up, socket open, connect sent; returns the policy waiting for the server's reply.
Policy connecting(uint64_t now_ms = 0) {
    Policy policy;
    assert(policy.wifi_up(now_ms) == Action::open);
    assert(policy.opened(now_ms) == Action::send_connect);
    return policy;
}

// Fails one attempt by closing the socket, then waits out the backoff to the next open.
uint64_t fail_and_retry(Policy& policy, uint64_t now_ms) {
    assert(policy.closed(now_ms, kExact) == Action::none);
    const uint64_t due = *policy.next_deadline_ms();
    assert(policy.tick(due - 1, kExact) == Action::none);
    assert(policy.tick(due, kExact) == Action::open);
    return due;
}

void waits_for_wifi_then_connects() {
    Policy policy;
    assert(policy.state() == State::reconnecting);
    assert(policy.wifi_up(500) == Action::open);
    assert(policy.opened(800) == Action::send_connect);
    assert(policy.state() == State::reconnecting);
    assert(!policy.application_frames_allowed());
}

void paired_or_pending_is_online() {
    Policy paired = connecting();
    assert(paired.received(100, Frame::paired) == Action::none);
    assert(paired.state() == State::online);
    assert(paired.application_frames_allowed());
    assert(!paired.next_deadline_ms());

    Policy pending = connecting();
    assert(pending.received(100, Frame::pairing_pending) == Action::none);
    assert(pending.state() == State::online);
    // Notifications need paired first.
    assert(!pending.application_frames_allowed());
    // A replacement code, then the owner claims the device.
    assert(pending.received(200, Frame::pairing_pending) == Action::none);
    assert(pending.received(300, Frame::paired) == Action::none);
    assert(pending.application_frames_allowed());
}

void no_reply_within_ten_seconds_is_a_failure() {
    Policy policy = connecting(1'000);
    assert(*policy.next_deadline_ms() == 1'000 + kReplyTimeoutMs);
    assert(policy.tick(1'000 + kReplyTimeoutMs - 1, kExact) == Action::none);
    assert(policy.tick(1'000 + kReplyTimeoutMs, kExact) == Action::close);
    assert(policy.closed(12'000, kExact) == Action::none);
    assert(policy.state() == State::reconnecting);
    assert(*policy.next_deadline_ms() == 12'000 + kFirstDelayMs);
}

void unexpected_frames_before_paired_are_dropped() {
    Policy policy = connecting();
    // Not an answer: dropped, and the reply deadline keeps running.
    assert(policy.received(100, Frame::other) == Action::none);
    assert(policy.state() == State::reconnecting);
    assert(*policy.next_deadline_ms() == kReplyTimeoutMs);
    // Frames before the socket opens, or after it closes, change nothing.
    Policy idle;
    assert(idle.received(0, Frame::paired) == Action::none);
    assert(idle.state() == State::reconnecting);
    assert(idle.opened(0) == Action::none);
    assert(idle.closed(0, kExact) == Action::none);
}

void retries_with_backoff_and_goes_offline_after_five_failures() {
    Policy policy;
    assert(policy.wifi_up(0) == Action::open);
    uint64_t now = 0;
    for (uint32_t failures = 1; failures < kOfflineAfter; ++failures) {
        now = fail_and_retry(policy, now);
        assert(policy.state() == State::reconnecting);
    }
    // Waits so far: 1 + 2 + 4 + 8 s.
    assert(now == 15'000);
    assert(policy.closed(now, kExact) == Action::none);
    assert(policy.state() == State::offline);
    // Offline still retries, after 16 s.
    assert(*policy.next_deadline_ms() == now + 16'000);
    assert(policy.tick(now + 16'000, kExact) == Action::open);
    assert(policy.state() == State::offline);
    // Back online resets both the state and the backoff.
    assert(policy.opened(now + 17'000) == Action::send_connect);
    assert(policy.received(now + 17'500, Frame::paired) == Action::none);
    assert(policy.state() == State::online);
    assert(policy.closed(now + 20'000, kExact) == Action::none);
    assert(policy.state() == State::reconnecting);
    assert(*policy.next_deadline_ms() == now + 20'000 + kFirstDelayMs);
}

void a_dropped_connection_retries_after_a_second() {
    Policy policy = connecting();
    policy.received(100, Frame::paired);
    // A missed pong or a server restart both arrive as a close.
    assert(policy.closed(60'000, kExact) == Action::none);
    assert(policy.state() == State::reconnecting);
    assert(!policy.application_frames_allowed());
    assert(policy.tick(61'000, kExact) == Action::open);
}

void losing_wifi_closes_and_its_return_retries_at_once() {
    Policy policy = connecting();
    policy.received(100, Frame::paired);
    assert(policy.wifi_down(5'000) == Action::close);
    assert(policy.closed(5'000, kExact) == Action::none);
    assert(policy.state() == State::reconnecting);
    // Without Wi-Fi each due retry fails at once, so the device still reaches offline.
    uint64_t now = 5'000;
    for (uint32_t failures = 1; failures < kOfflineAfter; ++failures) {
        now = *policy.next_deadline_ms();
        assert(policy.tick(now, kExact) == Action::none);
    }
    assert(policy.state() == State::offline);
    // Wi-Fi back: try now instead of waiting out the backoff.
    assert(policy.wifi_up(now + 100) == Action::open);
    // Wi-Fi during an attempt changes nothing.
    assert(policy.wifi_up(now + 200) == Action::none);
}

void a_stuck_open_is_abandoned() {
    Policy policy;
    assert(policy.wifi_up(0) == Action::open);
    assert(*policy.next_deadline_ms() == kOpenTimeoutMs);
    assert(policy.tick(kOpenTimeoutMs, kExact) == Action::close);
    assert(policy.closed(kOpenTimeoutMs, kExact) == Action::none);
    assert(policy.state() == State::reconnecting);
}

void malformed_or_busy_shows_a_server_error_and_retries() {
    for (const Reject reason : { Reject::malformed_request, Reject::server_busy }) {
        Policy policy = connecting();
        assert(policy.received(100, Frame::rejected, reason) == Action::close);
        assert(policy.closed(200, kExact) == Action::none);
        assert(policy.state() == State::server_error);
        assert(*policy.next_deadline_ms() == 200 + kFirstDelayMs);
        assert(policy.tick(1'200, kExact) == Action::open);
    }
}

void unsupported_version_stops_retrying() {
    Policy policy = connecting();
    assert(policy.received(100, Frame::rejected, Reject::unsupported_version) == Action::close);
    assert(policy.closed(200, kExact) == Action::none);
    assert(policy.state() == State::update_required);
    assert(!policy.next_deadline_ms());
    assert(policy.tick(1'000'000, kExact) == Action::none);
    assert(policy.wifi_down(1'000'000) == Action::none);
    assert(policy.wifi_up(1'000'001) == Action::none);
    assert(policy.state() == State::update_required);
}

void a_revoked_credential_reconnects_without_backoff() {
    Policy policy;
    policy.wifi_up(0);
    uint64_t now = fail_and_retry(policy, 0);
    now = fail_and_retry(policy, now);
    policy.opened(now);
    policy.received(now, Frame::paired);
    assert(policy.received(now + 100, Frame::rejected, Reject::credential_revoked) == Action::close);
    assert(policy.closed(now + 200, kExact) == Action::open);
    assert(policy.state() == State::reconnecting);
    // The backoff starts over: the next failure waits a second.
    assert(policy.closed(now + 300, kExact) == Action::none);
    assert(*policy.next_deadline_ms() == now + 300 + kFirstDelayMs);
}

void a_rejection_counts_once_when_the_close_follows() {
    Policy policy = connecting();
    policy.received(100, Frame::rejected, Reject::server_busy);
    // Frames after a rejection are ignored, and so is a second close report.
    assert(policy.received(150, Frame::paired) == Action::none);
    assert(policy.closed(200, kExact) == Action::none);
    assert(policy.closed(300, kExact) == Action::none);
    assert(policy.state() == State::server_error);
    assert(*policy.next_deadline_ms() == 200 + kFirstDelayMs);
}

void reaching_the_server_for_the_first_time() {
    Policy policy;
    assert(policy.status() == Status::disconnected);
    policy.wifi_up(0);
    policy.opened(100);
    assert(policy.status() == Status::disconnected);
    // An unreachable server, a bad address and a TLS failure all end the attempt: show the
    // error at once, and keep showing it through the retries.
    policy.closed(200, kExact);
    assert(policy.status() == Status::error);
    assert(policy.tick(*policy.next_deadline_ms(), kExact) == Action::open);
    assert(policy.status() == Status::error);
    // Until the server answers.
    policy.opened(2'000);
    policy.received(2'100, Frame::pairing_pending);
    assert(policy.status() == Status::pairing);
    policy.received(3'000, Frame::paired);
    assert(policy.status() == Status::connected);
}

void losing_wifi_is_not_a_server_error() {
    Policy policy;
    policy.wifi_up(0);
    assert(policy.wifi_down(100) == Action::close);
    policy.closed(100, kExact);
    assert(policy.status() == Status::disconnected);
    // A server failure, then Wi-Fi drops and returns: try the server again before blaming it.
    policy.wifi_up(200);
    policy.closed(300, kExact);
    assert(policy.status() == Status::error);
    policy.wifi_down(400);
    assert(policy.wifi_up(500) == Action::open);
    assert(policy.status() == Status::disconnected);
}

void a_lost_connection_shows_its_last_answer_until_offline() {
    for (const Frame answer : { Frame::paired, Frame::pairing_pending }) {
        // Paired: reconnecting. Waiting for a claim: still pairing, so the code stays up.
        const Status shown = answer == Frame::paired ? Status::reconnecting : Status::pairing;
        const Status answered = answer == Frame::paired ? Status::connected : Status::pairing;
        Policy policy = connecting();
        policy.received(100, answer);
        assert(policy.status() == answered);
        uint64_t now = 1'000;
        for (uint32_t failures = 1; failures < kOfflineAfter; ++failures) {
            now = fail_and_retry(policy, now);
            assert(policy.status() == shown);
        }
        assert(policy.status() == shown);
        policy.closed(now, kExact);
        assert(policy.state() == State::offline);
        assert(policy.status() == Status::error);
        // Answered again: back to normal.
        policy.tick(*policy.next_deadline_ms(), kExact);
        policy.opened(now + 20'000);
        policy.received(now + 20'100, answer);
        assert(policy.status() == answered);
    }
}

void rejections_show_an_error_or_an_update() {
    Policy busy = connecting();
    busy.received(100, Frame::paired);
    busy.received(200, Frame::rejected, Reject::server_busy);
    busy.closed(300, kExact);
    assert(busy.status() == Status::error);
    // Still an error while it retries.
    assert(busy.tick(*busy.next_deadline_ms(), kExact) == Action::open);
    assert(busy.status() == Status::error);

    Policy old = connecting();
    old.received(100, Frame::rejected, Reject::unsupported_version);
    old.closed(200, kExact);
    assert(old.status() == Status::update_required);
}

void time_without_wifi_does_not_slow_the_first_retry() {
    // Boot: Wi-Fi takes a while, and each due retry without it counts as a failure.
    Policy policy;
    uint64_t now = 0;
    for (uint32_t failures = 0; failures < kOfflineAfter; ++failures) {
        now = *policy.next_deadline_ms();
        assert(policy.tick(now, kExact) == Action::none);
    }
    assert(policy.state() == State::offline);
    // Wi-Fi comes up and the first real attempt fails: the next waits a second, not a minute.
    assert(policy.wifi_up(now) == Action::open);
    assert(policy.closed(now + 100, kExact) == Action::none);
    assert(*policy.next_deadline_ms() == now + 100 + kFirstDelayMs);
    assert(policy.state() == State::reconnecting);
}

void a_renewed_address_keeps_the_backoff() {
    Policy policy;
    policy.wifi_up(0);
    uint64_t now = fail_and_retry(policy, 0);
    now = fail_and_retry(policy, now);
    // Wi-Fi was never down: "Wi-Fi up" again changes nothing.
    assert(policy.wifi_up(now) == Action::none);
    assert(policy.closed(now, kExact) == Action::none);
    assert(*policy.next_deadline_ms() == now + 4'000);
    assert(policy.status() == Status::error);
}

void a_restored_pairing_reconnects_quietly() {
    Policy policy;
    policy.restore_paired();
    assert(policy.status() == Status::reconnecting);
    policy.wifi_up(0);
    // A failure while restoring isn't a setup error: still reconnecting until offline.
    policy.closed(100, kExact);
    assert(policy.status() == Status::reconnecting);
    assert(policy.tick(*policy.next_deadline_ms(), kExact) == Action::open);
    policy.opened(2'000);
    assert(policy.received(2'100, Frame::paired) == Action::none);
    assert(policy.status() == Status::connected);
    // The server no longer knows it: waiting for a claim again.
    Policy forgotten;
    forgotten.restore_paired();
    forgotten.wifi_up(0);
    forgotten.opened(100);
    forgotten.received(200, Frame::pairing_pending);
    assert(forgotten.status() == Status::pairing);
}

void a_removed_device_starts_over() {
    Policy policy = connecting();
    policy.received(100, Frame::paired);
    policy.received(200, Frame::rejected, Reject::credential_revoked);
    assert(policy.closed(300, kExact) == Action::open);
    // Unpaired, reaching the server for a new code.
    assert(policy.status() == Status::disconnected);
    policy.opened(400);
    policy.received(500, Frame::pairing_pending);
    assert(policy.status() == Status::pairing);
}
} // namespace

int main() {
    waits_for_wifi_then_connects();
    paired_or_pending_is_online();
    no_reply_within_ten_seconds_is_a_failure();
    unexpected_frames_before_paired_are_dropped();
    retries_with_backoff_and_goes_offline_after_five_failures();
    a_dropped_connection_retries_after_a_second();
    losing_wifi_closes_and_its_return_retries_at_once();
    a_stuck_open_is_abandoned();
    malformed_or_busy_shows_a_server_error_and_retries();
    unsupported_version_stops_retrying();
    a_revoked_credential_reconnects_without_backoff();
    a_rejection_counts_once_when_the_close_follows();
    reaching_the_server_for_the_first_time();
    losing_wifi_is_not_a_server_error();
    a_lost_connection_shows_its_last_answer_until_offline();
    rejections_show_an_error_or_an_update();
    time_without_wifi_does_not_slow_the_first_retry();
    a_renewed_address_keeps_the_backoff();
    a_restored_pairing_reconnects_quietly();
    a_removed_device_starts_over();
    std::puts("Policy tests passed");
}
