#include "../status.hpp"

#include <cassert>
#include <initializer_list>

using namespace mosaic::events;

int main() {
    for (Status value : { Status::disconnected, Status::pairing, Status::connected, Status::error }) {
        Status parsed = Status::error;
        assert(parse(name(value), parsed));
        assert(parsed == value);
    }
    for (const char* text : { "", "Connected", "connected ", "connect", "notification", "unknown" }) {
        Status parsed = Status::pairing;
        assert(!parse(text, parsed));
        assert(parsed == Status::pairing);
    }

    // Connecting pairs; an error keeps whatever came before; unpairing clears it.
    static_assert(paired_after(false, Status::connected));
    static_assert(paired_after(true, Status::error));
    static_assert(!paired_after(false, Status::error));
    static_assert(!paired_after(true, Status::disconnected));
    static_assert(!paired_after(true, Status::pairing));
}
