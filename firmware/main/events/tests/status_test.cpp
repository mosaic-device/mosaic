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
}
