#pragma once
namespace mosaic::ui::components {
enum class Request { activate, select, toggle };
using OnRequest = void (*)(Request, void*);
struct Action {
    OnRequest callback = nullptr;
    void* context = nullptr;
};
struct State {
    bool selected = false;
    bool focused = false;
    bool disabled = false;
    bool busy = false;
};
inline void request(State state, Action action, Request value) {
    if (!state.disabled && !state.busy && action.callback)
        action.callback(value, action.context);
}
}
