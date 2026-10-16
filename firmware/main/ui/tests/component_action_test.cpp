#include "../components/action.hpp"
#include <cassert>
#include <cstdio>
using namespace mosaic::ui::components;
struct Owner {
    unsigned calls = 0;
    Request last = Request::activate;
};
void receive(Request value, void* context) {
    auto& owner = *static_cast<Owner*>(context);
    ++owner.calls;
    owner.last = value;
}
int main() {
    Owner owner;
    Action action { receive, &owner };
    for (unsigned bits = 0; bits < 16; ++bits) {
        State state { bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8) };
        const auto before = owner.calls;
        request(state, action, Request::toggle);
        assert(owner.calls == before + ((!state.disabled && !state.busy) ? 1 : 0));
        assert(state.selected == bool(bits & 1));
        assert(state.focused == bool(bits & 2));
    }
    request({ }, action, Request::select);
    assert(owner.last == Request::select);
    request({ }, action, Request::activate);
    assert(owner.last == Request::activate);
    request({ }, { }, Request::toggle);
    std::puts("component action contracts passed");
}
