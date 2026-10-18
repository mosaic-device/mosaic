#include "../latest_state.hpp"
#include <cassert>
#include <mutex>
#include <thread>
int main() {
    mosaic::ui::LatestState<int> slot;
    int value = 0;
    uint64_t revision = 0;
    assert(!slot.read(value, revision));
    for (int i = 1; i <= 100; ++i)
        assert(slot.publish(i));
    assert(slot.read(value, revision) && value == 100);
    const auto older = revision;
    assert(slot.publish(101));
    slot.consumed(older);
    assert(slot.read(value, revision) && value == 101);
    slot.consumed(revision);
    assert(!slot.publish(101));
    assert(!slot.read(value, revision));
    slot.replay();
    assert(slot.read(value, revision) && value == 101);
    std::mutex lock;
    std::thread publisher([&] {
        for (int i = 102; i <= 10000; ++i) {
            std::lock_guard guard(lock);
            slot.publish(i);
        }
    });
    for (int i = 0; i < 10000; ++i) {
        {
            std::lock_guard guard(lock);
            slot.read(value, revision);
        }
        {
            std::lock_guard guard(lock);
            slot.consumed(revision);
        }
    }
    publisher.join();
    slot.replay();
    assert(slot.read(value, revision) && value == 10000);
}
