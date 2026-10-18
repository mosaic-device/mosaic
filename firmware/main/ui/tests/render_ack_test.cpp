#include "../render_ack.hpp"

#include <cassert>
#include <cstdint>

using mosaic::ui::RenderAck;

int main() {
    RenderAck wrap_tokens(UINT32_MAX - 1);
    const auto max_token = wrap_tokens.begin(100);
    assert(max_token == UINT32_MAX);
    wrap_tokens.cancel(max_token);
    const auto wrapped_token = wrap_tokens.begin(100);
    assert(wrapped_token == 1);
    wrap_tokens.note_flush(max_token, 1);
    wrap_tokens.acknowledge_refresh(max_token, 1);
    assert(!wrap_tokens.complete(wrapped_token, 1));
    RenderAck gate;
    const auto first = gate.begin(2'000);
    assert(first != 0);
    // The caller may have accepted/enqueued the request, but it is not rendered.
    assert(gate.current(first, 100));
    assert(!gate.acknowledged(first, 100));
    assert(!gate.complete(first, 100));
    assert(!gate.completed(first));

    gate.acknowledge_refresh(first, 150); // a refresh without a flush is not enough
    assert(!gate.acknowledged(first, 150));
    gate.note_flush(first + 1, 150);
    gate.acknowledge_refresh(first + 1, 150);
    assert(!gate.acknowledged(first, 150));
    gate.note_flush(first, 150);
    gate.acknowledge_refresh(first, 150);
    assert(gate.acknowledged(first, 150));
    assert(gate.pending(first));
    assert(!gate.completed(first)); // acknowledgement alone must not release the frame barrier
    assert(gate.complete(first, 150));
    assert(!gate.pending(first));
    assert(gate.completed(first));

    const auto second = gate.begin(3'000);
    assert(gate.completed(first)); // previous displayed QR stays up until replaced
    // A stale queued token and its old refresh cannot render/confirm the new request.
    assert(!gate.current(first, 200));
    gate.acknowledge_refresh(first, 200);
    assert(!gate.acknowledged(second, 200));
    assert(!gate.complete(second, 200));
    assert(gate.current(second, 200));
    gate.cancel(second);
    gate.acknowledge_refresh(second, 210); // late callback
    assert(!gate.current(second, 210));
    assert(!gate.acknowledged(second, 210));
    assert(!gate.completed(second));

    const auto third = gate.begin(500);
    assert(!gate.current(second, 300)); // old queue entry cannot use the new slot
    assert(gate.current(third, 499));
    gate.note_flush(third, 500);
    gate.acknowledge_refresh(third, 500); // deadline is exclusive
    assert(!gate.acknowledged(third, 500));
    assert(!gate.complete(third, 500));
    gate.cancel(third);
    gate.acknowledge_refresh(third, 501);
    assert(!gate.completed(third));

    // Even an earlier refresh cannot open the gate if the caller missed its deadline.
    const auto expired = gate.begin(1'000);
    gate.note_flush(expired, 999);
    gate.acknowledge_refresh(expired, 999);
    assert(!gate.complete(expired, 1'000));
    assert(gate.pending(expired)); // deadline expiry does not impersonate waiter cancellation
    assert(!gate.completed(expired));
    gate.cancel(expired);
    assert(!gate.pending(expired));
    gate.acknowledge_refresh(expired, 1'001);
    assert(!gate.completed(expired));

    // Millisecond clock wrap must not turn a valid short deadline into a timeout.
    const auto wrapped = gate.begin(5);
    assert(gate.current(wrapped, UINT32_MAX - 5));
    gate.note_flush(wrapped, UINT32_MAX - 5);
    gate.acknowledge_refresh(wrapped, UINT32_MAX - 5);
    assert(gate.complete(wrapped, UINT32_MAX - 4));
    assert(gate.completed(wrapped));
}
