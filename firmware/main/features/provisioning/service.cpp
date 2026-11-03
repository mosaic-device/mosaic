#include "service.hpp"
#include "state_machine.hpp"
#include "wifi_observation.hpp"

#include "adapters/captive_portal.hpp"
#include "adapters/settings_store.hpp"
#include "adapters/wifi.hpp"

#include <atomic>
#include <cstdio>

#include "../../support/secure_erase.hpp"
#include "../../ui/runtime.hpp"
#include "../../drivers/display.hpp"
#include "presentation/presenter.hpp"
#include "presentation/action_admission.hpp"
#include "ui/renderer.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace mosaic::features::provisioning {
namespace {
    namespace state = mosaic::features::provisioning;
    namespace provisioning_wifi = adapters::wifi;
    namespace provisioning_portal = adapters::portal;
    namespace provisioning_settings = adapters::settings;
    namespace ui = mosaic::ui;
    using form::Settings;
    uint32_t setup_session = 0;
    ActionAdmission admission;
    std::atomic_uint32_t action_context { 1 };
    portMUX_TYPE admission_mux = portMUX_INITIALIZER_UNLOCKED;
    enum class Kind : uint8_t { submit, recovery, got_ip, connected, disconnected };
    struct Intent {
        Kind kind;
        uint32_t wifi_sequence = 0;
        uint64_t admission_id = 0;
        uint32_t action_context = 0;
    };

    QueueHandle_t queue = nullptr;
    TaskHandle_t owner = nullptr;
    TaskHandle_t startup_caller = nullptr;
    std::atomic<state::PortalStatus> published_status { state::PortalStatus::idle };
    std::atomic_bool wifi_overflow { false };
    std::atomic_uint32_t wifi_sequence { 0 };
    portMUX_TYPE submission_mux = portMUX_INITIALIZER_UNLOCKED;
    Settings pending_submission { };
    bool submission_available = false;
    provisioning_wifi::Ap ap { };
    Settings saved { };
    esp_err_t startup_error = ESP_OK;
    bool recovery_callback_installed = false;
    WifiObservation observation; // provisioning owner task only
    using support::secure_erase;

    bool submit(const Settings& settings, void*) {
        if (!queue)
            return false;
        portENTER_CRITICAL(&submission_mux);
        if (submission_available) {
            portEXIT_CRITICAL(&submission_mux);
            return false;
        }
        pending_submission = settings;
        submission_available = true;
        portEXIT_CRITICAL(&submission_mux);
        const Intent intent { Kind::submit };
        if (xQueueSend(queue, &intent, 0) == pdTRUE)
            return true;
        portENTER_CRITICAL(&submission_mux);
        secure_erase(&pending_submission, sizeof(pending_submission));
        submission_available = false;
        portEXIT_CRITICAL(&submission_mux);
        return false;
    }
    bool take_submission(Settings& settings) {
        portENTER_CRITICAL(&submission_mux);
        const bool available = submission_available;
        if (available) {
            settings = pending_submission;
            secure_erase(&pending_submission, sizeof(pending_submission));
            submission_available = false;
        }
        portEXIT_CRITICAL(&submission_mux);
        return available;
    }
    bool on_action(UiAction action, void*) {
        if (action != UiAction::SetupTapped || !queue)
            return false;
        portENTER_CRITICAL(&admission_mux);
        const uint64_t id = admission.issue();
        const uint32_t context = action_context.load();
        Intent intent { Kind::recovery, 0, id, context };
        const bool accepted = xQueueSend(queue, &intent, 0) == pdTRUE;
        if (!accepted)
            admission.rejected(id, context);
        portEXIT_CRITICAL(&admission_mux);
        return accepted;
    }
    void post(Kind kind) {
        if (!queue)
            return;
        Intent intent { kind, ++wifi_sequence };
        if (xQueueSend(queue, &intent, 0) != pdTRUE)
            wifi_overflow.store(true);
    }
    void on_wifi(provisioning_wifi::Event event) {
        using E = provisioning_wifi::Event;
        post(event == E::connected ? Kind::connected : event == E::disconnected ? Kind::disconnected : Kind::got_ip);
    }

    void stop_wifi_safely() {
        if (!provisioning_wifi::initialized())
            return;
        const esp_err_t err = provisioning_wifi::stop();
        if (err != ESP_OK) {
            ESP_LOGE("mosaic_network", "Cannot stop Wi-Fi after setup failure: %s; restarting", esp_err_to_name(err));
            esp_restart();
        }
    }

    void stop_portal_safely() {
        // A second bounded wait reaps a worker that completed after the first timeout.
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (provisioning_portal::stop()) {

                return;
            }
        }
        ESP_LOGE("mosaic_network", "Portal shutdown incomplete; retaining dependencies and restarting");
        esp_restart();
    }

    void begin_wifi_observation_window() {
        wifi_overflow.exchange(false);
        observation.ignore_through(wifi_sequence.load());
    }

    esp_err_t display_setup() {
        char url[40];
        std::snprintf(url, sizeof(url), "http://%s/", ap.ip);
        return render_setup_credentials(ap.ssid, ap.key, url, setup_session);
    }

    esp_err_t open_portal() {
        // The synchronous display refresh/backlight is the gate before AP or HTTP/DNS.
        esp_err_t err = provisioning_wifi::configure_ap(ap);
        if (err == ESP_OK)
            err = display_setup();
        if (err == ESP_OK) {

            err = provisioning_wifi::start_ap();
        }
        if (err == ESP_OK) {

            if (!provisioning_portal::start(
                    { provisioning_wifi::ap_netif(), &published_status, submit, nullptr, ap.ip }))
                err = ESP_FAIL;
        }
        if (err != ESP_OK) {
            ESP_LOGE("mosaic_network", "Setup AP/display/portal failed: %s", esp_err_to_name(err));
            // Disconnect AP clients before waiting for synchronous HTTP shutdown.
            stop_wifi_safely();
            stop_portal_safely();
        }
        return err;
    }

    void dispatch(state::StateMachine& model, const state::Event& event);

    void execute(state::StateMachine& model, state::Effect& effect) {
        using E = state::EffectType;
        esp_err_t err = ESP_OK;
        switch (effect.type) {
        case E::start_saved_connection:
            begin_wifi_observation_window();
            err = provisioning_wifi::start_saved(saved);
            if (err != ESP_OK)
                startup_error = err;
            dispatch(model, state::StartupCompleted { err == ESP_OK });
            break;
        case E::open_portal:
            err = open_portal();
            if (err != ESP_OK)
                startup_error = err;
            dispatch(model, state::StartupCompleted { err == ESP_OK });
            break;
        case E::request_setup_reboot:
            err = provisioning_settings::set_setup_requested(true);
            dispatch(model, state::SetupRequested { err == ESP_OK });
            break;
        case E::reboot:
            drivers::display::prepare_reboot();
            esp_restart();
            break;
        case E::persist_candidate:

            err = provisioning_settings::persist(effect.settings);
            if (err == ESP_OK)
                saved = effect.settings;
            dispatch(model, state::Saved { err == ESP_OK });
            break;

        case E::reconnect:
            // The adapter stops/restarts STA; do not also connect or disconnect here.
            begin_wifi_observation_window();
            err = provisioning_wifi::reconnect(saved);
            dispatch(model, state::ReconnectCompleted { err == ESP_OK });
            break;
        case E::stop_portal_and_wifi:
            stop_wifi_safely();
            stop_portal_safely();
            break;
        }
        if (err != ESP_OK)
            ESP_LOGW("mosaic_network", "Provisioning effect %u failed: %s", static_cast<unsigned>(effect.type),
                esp_err_to_name(err));
        secure_erase(&effect.settings, sizeof(effect.settings));
    }
    void dispatch_at(state::StateMachine& model, const state::Event& event, int64_t now) {
        auto effects = model.dispatch(event, now);
        const auto snapshot = model.snapshot();
        if (snapshot.phase == Phase::opening_portal && !setup_session)
            ++setup_session;
        action_context.store(snapshot.action_context);
        published_status.store(snapshot.portal_status);
        publish_view(present(snapshot, setup_session));
        for (size_t i = 0; i < effects.count; ++i)
            execute(model, effects.effects[i]);
        // StateMachine effects can contain copies of credentials; erase even unused slots.
        secure_erase(&effects, sizeof(effects));
    }
    void dispatch(state::StateMachine& model, const state::Event& event) {
        dispatch_at(model, event, esp_timer_get_time());
    }

    void resync_wifi(state::StateMachine& model) {
        ESP_LOGE("mosaic_network", "Wi-Fi event queue overflow; resynchronizing radio state");
        const auto phase = model.snapshot().phase;
        if (!follows_station(phase))
            return;
        const char* ssid = needs_saved_network(phase) ? saved.ssid : nullptr;
        const bool connected = provisioning_wifi::associated(ssid);
        const auto events = resync(phase, { connected, connected && provisioning_wifi::has_ip(ssid) });
        for (size_t i = 0; i < events.count; ++i)
            dispatch(model, events.events[i]);
    }

    esp_err_t detach_recovery_callback() {
        if (!recovery_callback_installed)
            return ESP_OK;
        const esp_err_t err = bind_action(nullptr, nullptr);
        if (err == ESP_OK)
            recovery_callback_installed = false;
        return err;
    }

    void release_owner_storage() {
        provisioning_wifi::release();
        if (queue) {
            vQueueDelete(queue);
            queue = nullptr;
        }
        portENTER_CRITICAL(&submission_mux);
        secure_erase(pending_submission);
        submission_available = false;
        portEXIT_CRITICAL(&submission_mux);
        secure_erase(saved);
        secure_erase(ap);
        owner = nullptr;
    }

    esp_err_t cleanup() {
        stop_wifi_safely(); // No-op before successful Wi-Fi initialization.
        esp_err_t err = detach_recovery_callback();
        if (err != ESP_OK)
            return err;
        stop_portal_safely();
        err = provisioning_wifi::deinit();
        if (err != ESP_OK)
            return err;
        release_owner_storage();
        return ESP_OK;
    }

    void cleanup_startup_failure(esp_err_t startup_result) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            const esp_err_t err = cleanup();
            if (err == ESP_OK)
                return;
            ESP_LOGE("mosaic_network", "Startup error %s; cleanup failed: %s", esp_err_to_name(startup_result),
                esp_err_to_name(err));
        }
        // Never free queues/netifs while a callback or worker can still use them.
        ESP_LOGE("mosaic_network", "Startup cleanup incomplete; retaining dependencies and restarting");
        esp_restart();
    }
    esp_err_t initialize(state::StateMachine& model, const char*& stage) {
        stage = "UI readiness";
        if (!ui::ready())
            return ESP_ERR_INVALID_STATE;
        stage = "intent queue";
        queue = xQueueCreate(8, sizeof(Intent));
        if (!queue)
            return ESP_ERR_NO_MEM;
        stage = "saved settings";
        bool have_saved = false;
        esp_err_t err = provisioning_settings::load(saved, have_saved);
        if (err != ESP_OK)
            return err;
        bool setup_requested = false;
        stage = "setup request";
        err = provisioning_settings::load_setup_requested(setup_requested);
        if (err != ESP_OK)
            return err;
        err = provisioning_wifi::init(on_wifi, stage);
        if (err != ESP_OK)
            return err;
        stage = "recovery callback";
        err = bind_action(on_action, nullptr);
        if (err != ESP_OK)
            return err;
        recovery_callback_installed = true;
        stage = have_saved && !setup_requested ? "saved STA connection" : "setup AP/display/HTTP/DNS";
        dispatch(model, state::Start { have_saved, setup_requested });
        if (model.snapshot().phase == state::Phase::startup_failed)
            return startup_error == ESP_OK ? ESP_FAIL : startup_error;
        return ESP_OK;
    }

    void reconcile_lost_wifi_events(state::StateMachine& model) {
        if (!wifi_overflow.exchange(false))
            return;
        observation.ignore_through(wifi_sequence.load());
        resync_wifi(model);
    }

    bool stale_wifi_event(const Intent& event) {
        return event.kind != Kind::submit && event.kind != Kind::recovery && observation.stale(event.wifi_sequence);
    }

    void handle_event(state::StateMachine& model, const Intent& event) {
        if (stale_wifi_event(event))
            return;
        switch (event.kind) {
        case Kind::submit: {
            state::Event submission { std::in_place_type<state::Submit> };
            auto& credentials = std::get<state::Submit>(submission).settings;
            if (take_submission(credentials))
                dispatch(model, submission);
            secure_erase(credentials);
            provisioning_portal::consumed();
        } break;
        case Kind::recovery:
            dispatch(model, state::SetupTapped { event.admission_id, event.action_context });
            break;
        case Kind::connected:
            if (!needs_saved_network(model.snapshot().phase) || provisioning_wifi::associated(saved.ssid))
                dispatch(model, state::Connected { });
            break;
        case Kind::got_ip:
            reconcile_lost_wifi_events(model);
            if (!stale_wifi_event(event)
                && (!needs_saved_network(model.snapshot().phase) || provisioning_wifi::has_ip(saved.ssid)))
                dispatch(model, state::GotIp { });
            break;
        case Kind::disconnected:
            dispatch(model, state::Disconnected { });
            break;
        }
    }

    void drain_rejections(state::StateMachine& model) {
        portENTER_CRITICAL(&admission_mux);
        const auto observation = admission.take_rejected();
        portEXIT_CRITICAL(&admission_mux);
        if (observation.id)
            dispatch(model, ActionRejected { observation.id, observation.context });
    }

    void owner_loop(void*) {
        state::StateMachine model;
        const char* stage = nullptr;
        const esp_err_t result = initialize(model, stage);
        if (result != ESP_OK) {
            ESP_LOGE("mosaic_network", "Startup failed at %s: %s (0x%x)", stage, esp_err_to_name(result), result);
            cleanup_startup_failure(result);
            xTaskNotify(startup_caller, static_cast<uint32_t>(result), eSetValueWithOverwrite);
            vTaskDelete(nullptr);
            return;
        }
        xTaskNotify(startup_caller, static_cast<uint32_t>(result), eSetValueWithOverwrite);
        while (true) {
            reconcile_lost_wifi_events(model);
            Intent event { };
            if (xQueueReceive(queue, &event, pdMS_TO_TICKS(250)) == pdTRUE) {
                reconcile_lost_wifi_events(model);
                drain_rejections(model);
                handle_event(model, event);
                secure_erase(event);
            }
            reconcile_lost_wifi_events(model);
            dispatch(model, state::Tick { });
            drain_rejections(model);
        }
    }
} // namespace

esp_err_t start() {
    if (owner)
        return ESP_OK;
    startup_caller = xTaskGetCurrentTaskHandle();
    if (xTaskCreate(owner_loop, "mosaic_network", 8192, nullptr, 5, &owner) != pdPASS) {
        startup_caller = nullptr;
        publish_view(present(StartupView::unavailable, setup_session));
        return ESP_ERR_NO_MEM;
    }
    uint32_t result = ESP_FAIL;
    xTaskNotifyWait(0, UINT32_MAX, &result, portMAX_DELAY);
    startup_caller = nullptr;
    if (result != ESP_OK)
        publish_view(present(StartupView::unavailable, setup_session));
    return static_cast<esp_err_t>(result);
}
} // namespace mosaic::features::provisioning
