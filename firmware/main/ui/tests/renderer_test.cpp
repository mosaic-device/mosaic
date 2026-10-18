// Screen methods are recording seams; all mailbox/session/render selection is production code.
#include "../../features/provisioning/ui/renderer.cpp"
#include <cassert>
using namespace mosaic::features::provisioning;
namespace {
ViewState visible;
int status_renders = 0;
int credential_renders = 0;
int credential_clears = 0;
int failure_renders = 0;
bool runtime_ready = true;
ViewState during_render;
bool publish_during_render = false;
std::string_view failure_text;
}
namespace mosaic::features::provisioning {
bool StatusScreen::create(lv_obj_t*) {
    return true;
}
void StatusScreen::hide() { }
void StatusScreen::render(const ViewState& state) {
    visible = state;
    ++status_renders;
    if (publish_during_render) {
        publish_during_render = false;
        assert(publish_view(during_render) == ESP_OK);
    }
}
void StatusScreen::bind_action(ActionCallback callback, void* context) {
    action_callback = callback;
    action_context = context;
}
void SetupScreen::create(lv_obj_t*) { }
void SetupScreen::hide() {
    ++credential_clears;
}
void SetupScreen::render_failure(std::string_view text) {
    ++failure_renders;
    failure_text = text;
}
void SetupScreen::show(std::string_view ssid, std::string_view password, std::string_view url, std::string_view qr) {
    assert(ssid == "Mosaic-TEST" && password == "test-key" && url == "http://192.168.4.1/");
    assert(qr == "WIFI:T:WPA;S:Mosaic-TEST;P:test-key;;");
    ++credential_renders;
}
}
namespace mosaic::ui {
bool ready() {
    return runtime_ready;
}
esp_err_t render_credentials(const void* bytes, size_t size) {
    const auto callbacks = renderer_callbacks();
    return callbacks.credentials(bytes, size, callbacks.context) ? ESP_OK : ESP_FAIL;
}
}
namespace mosaic::drivers::display {
bool lock() {
    return true;
}
void unlock() { }
}
int main() {
    const auto callbacks = renderer_callbacks();
    assert(callbacks.create(nullptr, nullptr));
    ViewState first;
    first.message = "First";
    ViewState newest;
    newest.message = "Newest";
    for (int i = 0; i < 20; ++i) {
        assert(publish_view(first) == ESP_OK);
        assert(publish_view(newest) == ESP_OK);
    }
    callbacks.routine(nullptr);
    assert(visible == newest);
    int count = status_renders;
    assert(publish_view(newest) == ESP_OK);
    callbacks.routine(nullptr);
    assert(count == status_renders);
    during_render = newest;
    during_render.message = "Concurrent";
    publish_during_render = true;
    publish_view(first);
    callbacks.routine(nullptr);
    callbacks.routine(nullptr);
    assert(visible == during_render); // consuming old revision did not clear concurrent publish
    assert(callbacks.create(nullptr, nullptr));
    callbacks.routine(nullptr);
    assert(visible == during_render); // recreation needs no command replay

    assert(render_setup_credentials("Mosaic-TEST", "test-key", "http://192.168.4.1/", 1) == ESP_OK);
    const int clears = credential_clears;
    publish_view(first); // stale startup session cannot hide the QR
    callbacks.routine(nullptr);
    assert(credential_clears == clears);
    ViewState setup_state;
    setup_state.session = 1;
    setup_state.screen = ScreenKind::setup;
    publish_view(setup_state);
    callbacks.routine(nullptr);
    setup_state.setup_failure = "Saving settings failed. Please try again.";
    publish_view(setup_state);
    callbacks.routine(nullptr);
    assert(!failure_text.empty());
    const int failures = failure_renders;
    publish_view(setup_state);
    callbacks.routine(nullptr);
    assert(failure_renders == failures && credential_renders == 1);
    newest.session = 1; // current session deliberately leaves setup after save
    publish_view(newest);
    callbacks.routine(nullptr);
    assert(credential_clears == clears + 1 && visible == newest);
    callbacks.cancel_credentials(nullptr);
    assert(credential_clears == clears + 2);
    // Credentials were acknowledged, but AP/HTTP startup then failed.
    StateMachine startup;
    (void)startup.dispatch(Start { false }, 0);
    assert(render_setup_credentials("Mosaic-TEST", "test-key", "http://192.168.4.1/", 1) == ESP_OK);
    const int failure_clears = credential_clears;
    const int failure_status_renders = status_renders;
    // An unrelated stale startup/unavailable view must still be rejected.
    assert(publish_view(present(StartupView::unavailable)) == ESP_OK);
    callbacks.routine(nullptr);
    assert(credential_clears == failure_clears && status_renders == failure_status_renders);
    assert(publish_view(present(StartupView::unavailable, 2)) == ESP_OK);
    callbacks.routine(nullptr);
    assert(credential_clears == failure_clears && status_renders == failure_status_renders);
    (void)startup.dispatch(StartupCompleted { false }, 1);
    const auto startup_failure = present(startup.snapshot(), 1);
    assert(startup_failure.session == 1);
    assert(publish_view(startup_failure) == ESP_OK);
    // The service's final error publication must not lose the same session.
    assert(publish_view(present(StartupView::unavailable, 1)) == ESP_OK);
    callbacks.routine(nullptr);
    assert(credential_clears == failure_clears + 1);
    assert(visible == startup_failure && visible.screen == ScreenKind::status);
    assert(visible.message.view().find("Setup unavailable") == 0);
    callbacks.routine(nullptr);
    assert(credential_clears == failure_clears + 1);

    runtime_ready = false;
    assert(publish_view(first) == ESP_ERR_INVALID_STATE);
    assert(bind_action(nullptr, nullptr) == ESP_ERR_INVALID_STATE);
}
