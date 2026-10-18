#include "service.hpp"
#include "state_machine.hpp"

#include "adapters/captive_portal.hpp"
#include "adapters/settings_store.hpp"
#include "adapters/wifi.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "../../support/secure_erase.hpp"
#include "../../ui/runtime.hpp"
#include "../../drivers/display.hpp"
#include "presentation/presenter.hpp"
#include "presentation/action_admission.hpp"
#include "ui/renderer.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
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
    esp_netif_t* ap_netif = nullptr;
    esp_netif_t* sta_netif = nullptr;
    esp_event_handler_instance_t wifi_handler = nullptr;
    esp_event_handler_instance_t ip_handler = nullptr;
    std::atomic<state::PortalStatus> published_status { state::PortalStatus::idle };
    std::atomic_bool wifi_overflow { false };
    std::atomic_uint32_t wifi_sequence { 0 };
    portMUX_TYPE submission_mux = portMUX_INITIALIZER_UNLOCKED;
    Settings pending_submission { };
    bool submission_available = false;
    provisioning_wifi::Ap ap { };
    Settings saved { };
    esp_err_t startup_error = ESP_OK;
    bool wifi_initialized = false; // provisioning owner task only
    bool recovery_callback_installed = false;
    uint32_t ignored_wifi_through = 0; // provisioning owner task only
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
    void on_wifi(void*, esp_event_base_t, int32_t id, void*) {
        if (id == WIFI_EVENT_STA_CONNECTED)
            post(Kind::connected);
        if (id == WIFI_EVENT_STA_DISCONNECTED)
            post(Kind::disconnected);
    }
    void on_ip(void*, esp_event_base_t, int32_t id, void*) {
        if (id == IP_EVENT_STA_GOT_IP)
            post(Kind::got_ip);
    }

    void stop_wifi_safely() {
        if (!wifi_initialized)
            return;
        const esp_err_t err = provisioning_wifi::stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
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
        ignored_wifi_through = wifi_sequence.load();
    }

    esp_err_t display_setup() {
        char url[40];
        std::snprintf(url, sizeof(url), "http://%s/", ap.ip);
        return render_setup_credentials(ap.ssid, ap.key, url, setup_session);
    }

    esp_err_t open_portal() {
        // The synchronous display refresh/backlight is the gate before AP or HTTP/DNS.
        esp_err_t err = provisioning_wifi::configure_ap(ap_netif, ap);
        if (err == ESP_OK)
            err = display_setup();
        if (err == ESP_OK) {

            err = provisioning_wifi::start_ap();
        }
        if (err == ESP_OK) {

            if (!provisioning_portal::start({ ap_netif, &published_status, submit, nullptr, ap.ip }))
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

    bool saved_station_associated() {
        wifi_ap_record_t record { };
        return esp_wifi_sta_get_ap_info(&record) == ESP_OK
            && std::strncmp(reinterpret_cast<const char*>(record.ssid), saved.ssid, sizeof(record.ssid)) == 0;
    }

    bool saved_station_has_ip() {
        esp_netif_ip_info_t info { };
        return saved_station_associated() && sta_netif && esp_netif_get_ip_info(sta_netif, &info) == ESP_OK
            && info.ip.addr != 0;
    }

    void resync_wifi(state::StateMachine& model) {
        ESP_LOGE("mosaic_network", "Wi-Fi event queue overflow; resynchronizing radio state");
        const auto phase = model.snapshot().phase;
        if (phase != state::Phase::saved_sta && phase != state::Phase::online && phase != state::Phase::offline)
            return;
        wifi_ap_record_t record { };
        esp_netif_ip_info_t info { };
        const bool connected = esp_wifi_sta_get_ap_info(&record) == ESP_OK
            && (phase != state::Phase::saved_sta
                || std::strncmp(reinterpret_cast<const char*>(record.ssid), saved.ssid, sizeof(record.ssid)) == 0);
        const bool has_ip = connected && sta_netif && esp_netif_get_ip_info(sta_netif, &info) == ESP_OK && info.ip.addr;
        if (connected)
            dispatch(model, state::Connected { });
        if (has_ip)
            dispatch(model, state::GotIp { });
        else
            dispatch(model, state::Disconnected { });
    }

    esp_err_t detach_recovery_callback() {
        if (!recovery_callback_installed)
            return ESP_OK;
        const esp_err_t err = bind_action(nullptr, nullptr);
        if (err == ESP_OK)
            recovery_callback_installed = false;
        return err;
    }

    esp_err_t unregister_wifi_handlers() {
        if (ip_handler) {
            const esp_err_t err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
            if (err != ESP_OK)
                return err;
            ip_handler = nullptr;
        }
        if (wifi_handler) {
            const esp_err_t err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
            if (err != ESP_OK)
                return err;
            wifi_handler = nullptr;
        }
        return ESP_OK;
    }

    void release_owner_storage() {
        if (sta_netif) {
            esp_netif_destroy_default_wifi(sta_netif);
            sta_netif = nullptr;
        }
        if (ap_netif) {
            esp_netif_destroy_default_wifi(ap_netif);
            ap_netif = nullptr;
        }
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
        err = unregister_wifi_handlers();
        if (err != ESP_OK)
            return err;
        if (wifi_initialized) {
            err = esp_wifi_deinit();
            if (err != ESP_OK)
                return err;
            wifi_initialized = false;
        }
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
    esp_err_t initialize_wifi(const char*& stage) {
        stage = "Wi-Fi netifs";
        ap_netif = esp_netif_create_default_wifi_ap();
        sta_netif = esp_netif_create_default_wifi_sta();
        if (!ap_netif || !sta_netif)
            return ESP_ERR_NO_MEM;
        stage = "Wi-Fi init";
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK)
            return err;
        wifi_initialized = true;
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err != ESP_OK)
            return err;
        stage = "Wi-Fi event registration";
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, nullptr, &wifi_handler);
        if (err != ESP_OK)
            return err;
        stage = "IP event registration";
        return esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, nullptr, &ip_handler);
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
        err = initialize_wifi(stage);
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
        ignored_wifi_through = wifi_sequence.load();
        resync_wifi(model);
    }

    bool stale_wifi_event(const Intent& event) {
        return event.kind != Kind::submit && event.kind != Kind::recovery
            && static_cast<int32_t>(event.wifi_sequence - ignored_wifi_through) <= 0;
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
            if (model.snapshot().phase != state::Phase::saved_sta || saved_station_associated())
                dispatch(model, state::Connected { });
            break;
        case Kind::got_ip:
            reconcile_lost_wifi_events(model);
            if (!stale_wifi_event(event)
                && (model.snapshot().phase != state::Phase::saved_sta || saved_station_has_ip()))
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
