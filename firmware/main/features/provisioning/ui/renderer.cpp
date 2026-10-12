#include "renderer.hpp"
#include "setup_screen.hpp"
#include "status_screen.hpp"
#include "../presentation/wifi_qr.hpp"
#include "../presentation/presenter.hpp"
#include "../../../ui/latest_state.hpp"
#include "../../../drivers/display.hpp"
#include "../../../support/secure_erase.hpp"
#include <cstring>
#include "freertos/FreeRTOS.h"
namespace mosaic::features::provisioning {
namespace {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    mosaic::ui::LatestState<ViewState> mailbox;
    SetupScreen setup;
    StatusScreen status;
    ViewState rendered { };
    bool has_rendered = false;
    uint32_t credential_session = 0; // LVGL worker only
    struct Credentials {
        uint32_t session;
        char ssid[33] { };
        char password[65] { };
        char url[193] { };
    };
    bool create(lv_obj_t* parent, void*) {
        if (!status.create(parent))
            return false;
        setup.create(parent);
        status.render(present(StartupView::starting));
        has_rendered = false;
        credential_session = 0;
        portENTER_CRITICAL(&mux);
        mailbox.replay();
        portEXIT_CRITICAL(&mux);
        return true;
    }
    void routine(void*) {
        ViewState state;
        uint64_t revision = 0;
        portENTER_CRITICAL(&mux);
        const bool pending = mailbox.read(state, revision);
        portEXIT_CRITICAL(&mux);
        if (!pending)
            return;
        // An old startup/session snapshot may never replace an acknowledged setup.
        // Authoritative startup failure carries the same session and retires it.
        if (!credential_session || state.session == credential_session) {
            if (!has_rendered || !(rendered == state)) {
                if (state.screen == ScreenKind::setup) {
                    if (credential_session == state.session && credential_session) {
                        status.hide();
                        setup.render_failure(state.setup_failure.view());
                    }
                } else {
                    setup.hide();
                    credential_session = 0;
                    status.render(state);
                }
                rendered = state;
                has_rendered = true;
            }
        }
        portENTER_CRITICAL(&mux);
        mailbox.consumed(revision);
        portEXIT_CRITICAL(&mux);
    }
    bool credentials(const void* bytes, size_t size, void*) {
        if (size != sizeof(Credentials))
            return false;
        Credentials value { };
        std::memcpy(&value, bytes, sizeof(value));
        char qr[256] { };
        if (!value.session || !wifi_join_payload(value.ssid, value.password, qr, sizeof(qr))) {
            support::secure_erase(qr);
            support::secure_erase(value);
            return false;
        }
        status.hide();
        setup.show(value.ssid, value.password, value.url, qr);
        support::secure_erase(qr);
        credential_session = value.session;
        support::secure_erase(value);
        has_rendered = false;
        // Restore the current routine failure state after the acknowledgement barrier.
        portENTER_CRITICAL(&mux);
        mailbox.replay();
        portEXIT_CRITICAL(&mux);
        return true;
    }
    void cancel(void*) {
        setup.hide();
        credential_session = 0;
        has_rendered = false;
        status.render(present(StartupView::starting));
    }
    bool valid(std::string_view value, size_t max) {
        return !value.empty() && value.size() <= max && value.find('\0') == std::string_view::npos;
    }
    void copy(char* out, std::string_view value) {
        std::memcpy(out, value.data(), value.size());
        out[value.size()] = '\0';
    }
}
mosaic::ui::Renderer renderer_callbacks() {
    return { create, routine, credentials, cancel, nullptr };
}
esp_err_t publish_view(const ViewState& state) {
    if (!mosaic::ui::ready())
        return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&mux);
    mailbox.publish(state);
    portEXIT_CRITICAL(&mux);
    return ESP_OK;
}
esp_err_t bind_action(ActionCallback callback, void* context) {
    if (!mosaic::ui::ready())
        return ESP_ERR_INVALID_STATE;
    if (!drivers::display::lock())
        return ESP_ERR_TIMEOUT;
    status.bind_action(callback, context);
    drivers::display::unlock();
    return ESP_OK;
}
esp_err_t render_setup_credentials(
    std::string_view ssid, std::string_view password, std::string_view url, uint32_t session) {
    if (!session || !valid(ssid, 32) || !valid(password, 64) || !valid(url, 192))
        return ESP_ERR_INVALID_ARG;
    char qr[256] { };
    const bool encodable = wifi_join_payload(ssid, password, qr, sizeof(qr));
    support::secure_erase(qr);
    if (!encodable)
        return ESP_ERR_INVALID_ARG;
    Credentials value { .session = session };
    copy(value.ssid, ssid);
    copy(value.password, password);
    copy(value.url, url);
    const esp_err_t result = mosaic::ui::render_credentials(&value, sizeof(value));
    support::secure_erase(value);
    return result;
}
}
