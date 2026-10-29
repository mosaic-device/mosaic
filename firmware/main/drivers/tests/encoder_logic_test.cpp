#include "../encoder_logic.hpp"

#include <cassert>
#include <cstdint>
#include <initializer_list>

using namespace mosaic::drivers::encoder;

namespace {
// Feeds a sequence of raw counts as if each were one LVGL read, returning the summed steps.
int32_t feed(DetentCounter& counter, std::initializer_list<int32_t> counts, bool at_rest) {
    int32_t total = 0;
    for (int32_t count : counts)
        total += counter.update(count, at_rest);
    return total;
}

void detents() {
    DetentCounter counter;
    assert(counter.update(4, true) == 1);
    assert(counter.update(0, true) == -1);
    assert(counter.update(-4, true) == -1);
    assert(counter.update(0, true) == 1);
    // Twenty detents one click at a time, then back.
    int32_t total = 0;
    for (int32_t i = 1; i <= 20; ++i)
        total += counter.update(i * kEdgesPerDetent, true);
    assert(total == 20);
    for (int32_t i = 19; i >= 0; --i)
        total += counter.update(i * kEdgesPerDetent, true);
    assert(total == 0);
}

void fast_turn() {
    // PCNT keeps counting between reads, so a fast spin arrives as one large jump.
    DetentCounter counter;
    assert(counter.update(40, true) == 10);
    assert(counter.update(-40, true) == -20);
    // A fast spin read mid-detent emits whole detents and keeps the partial one.
    assert(counter.update(-40 + 4 * 7 + 2, false) == 7);
    assert(counter.update(-40 + 4 * 8, true) == 1);
}

void partial_turns() {
    DetentCounter counter;
    // A slow turn read at every edge emits exactly once, on the fourth edge.
    assert(counter.update(1, false) == 0);
    assert(counter.update(2, false) == 0);
    assert(counter.update(3, false) == 0);
    assert(counter.update(4, true) == 1);
    // Half a turn then back to the same detent emits nothing.
    assert(feed(counter, { 5, 6, 5 }, false) == 0);
    assert(counter.update(4, true) == 0);
}

void no_dead_zone_around_zero() {
    // Truncating the absolute count (count / 4) maps -3..3 to the same step.
    // Counting relative to the last boundary must keep both sides symmetric.
    DetentCounter counter;
    assert(feed(counter, { 2, 0, -2, -4 }, false) == -1);
    assert(counter.update(-4, true) == 0);
    assert(feed(counter, { -2, 0 }, false) == 1);
    assert(counter.update(0, true) == 0);
    assert(feed(counter, { 4, 0, -4, 0, 4 }, true) == 1);
}

void bounce() {
    // Bounce on one phase produces alternating +1/-1 edges, never a step.
    DetentCounter counter;
    assert(feed(counter, { 1, 0, 1, 0, -1, 0, 1, 0 }, false) == 0);
    assert(counter.update(0, true) == 0);
    // Bouncing at each edge of a real click still gives exactly one step.
    assert(feed(counter, { 1, 0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4 }, false) == 1);
    assert(counter.update(4, true) == 0);
}

void realigns_after_lost_or_extra_edges() {
    {
        // A lost edge: the click ends three edges in. Rest counts it.
        DetentCounter counter;
        assert(counter.update(3, false) == 0);
        assert(counter.update(3, true) == 1);
        // The rest position is the new boundary, so the next click is exact.
        assert(counter.update(7, true) == 1);
        assert(counter.update(11, true) == 1);
    }
    {
        // An extra edge: five edges for one click. Rest drops the spare edge.
        DetentCounter counter;
        assert(counter.update(5, true) == 1);
        assert(counter.update(9, true) == 1);
        assert(counter.update(13, false) == 1);
        assert(counter.update(13, true) == 0);
    }
    {
        // The same in reverse.
        DetentCounter counter;
        assert(counter.update(-3, true) == -1);
        assert(counter.update(-7, true) == -1);
        assert(counter.update(-12, true) == -1);
        assert(counter.update(-16, true) == -1);
    }
    {
        // Without rest, an offset can't be corrected, but it still can't double-step.
        DetentCounter counter;
        assert(feed(counter, { 3, 7, 11 }, false) == 2);
        assert(counter.update(11, true) == 1);
    }
}

void counter_wraparound() {
    DetentCounter counter(INT32_MAX - 1);
    assert(counter.update(INT32_MIN + 2, true) == 1); // INT32_MAX - 1 + 4 wraps.
    assert(counter.update(INT32_MAX - 1, true) == -1);
    assert(counter.update(INT32_MIN + 6, false) == 2);
}

void press_latch() {
    {
        // A held press: pressed until its release arrives, then released.
        PressLatch latch;
        assert(!latch.update({ .downs = 0, .ups = 0 }));
        assert(latch.update({ .downs = 1, .ups = 0 }));
        assert(latch.update({ .downs = 1, .ups = 0 }));
        assert(!latch.update({ .downs = 1, .ups = 1 }));
        assert(!latch.update({ .downs = 1, .ups = 1 }));
    }
    {
        // A quick tap that starts and ends between two reads still gives LVGL
        // a pressed read followed by a released read.
        PressLatch latch;
        assert(latch.update({ .downs = 1, .ups = 1 }));
        assert(!latch.update({ .downs = 1, .ups = 1 }));
    }
    {
        // Two taps between reads replay as two clicks, never merged into one.
        PressLatch latch;
        int clicks = 0;
        bool was = false;
        for (int read = 0; read < 6; ++read) {
            const bool now = latch.update({ .downs = 2, .ups = 2 });
            if (was && !now)
                ++clicks;
            was = now;
        }
        assert(clicks == 2 && !was);
    }
    {
        // A tap, then a second press still held at the next read.
        PressLatch latch;
        assert(latch.update({ .downs = 2, .ups = 1 }));
        assert(!latch.update({ .downs = 2, .ups = 1 }));
        assert(latch.update({ .downs = 2, .ups = 1 }));
        assert(latch.update({ .downs = 2, .ups = 1 }));
    }
}
}

int main() {
    detents();
    fast_turn();
    partial_turns();
    no_dead_zone_around_zero();
    bounce();
    realigns_after_lost_or_extra_edges();
    counter_wraparound();
    press_latch();
}
