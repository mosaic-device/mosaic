// Host preview of the device screens using their real LVGL implementation.
#include "lvgl.h"
#include "ui/generated/design_tokens.h"
#include "ui/navigation/host.hpp"
#include "ui/styles/styles.hpp"
#include "features/home/screen.hpp"
#include "features/settings/screen.hpp"
#include "features/settings/service.hpp"
#include "features/clock/service.hpp"
#include "features/provisioning/presentation/presenter.hpp"
#include "features/provisioning/ui/status_screen.hpp"
#include "features/provisioning/ui/setup_screen.hpp"
#include "events/bus.hpp"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <SDL.h>
#include <array>
#include <filesystem>

using namespace mosaic;
namespace prov = features::provisioning;

// ---- stubs for device services --------------------------------------------------------
namespace {
lv_indev_t* g_encoder = nullptr;
int g_enc_diff = 0;
bool g_pressed = false;
bool g_touch = false;
int g_touch_x = 0, g_touch_y = 0;
uint32_t g_backs = 0;
events::Status g_status = events::Status::disconnected;
bool g_paired = false;
features::settings::Settings g_settings;
}
namespace mosaic::drivers::encoder {
void set_group(lv_group_t* group) {
    lv_indev_set_group(g_encoder, group);
}
uint32_t back_presses() {
    return g_backs;
}
}
namespace mosaic::events {
Status status() {
    return g_status;
}
bool paired() {
    return g_paired;
}
}
namespace mosaic::features::clock {
// 2026-11-03 14:05 local (MST7): 21:05Z.
constexpr std::time_t kNow = 1'793'739'900;
bool g_synced = true;
std::string_view now_text(std::span<char, kTextSize> b) {
    return text(g_synced, kNow, b);
}
std::string_view today_text(std::span<char, kDateSize> b) {
    return date(g_synced, kNow, b);
}
}
namespace mosaic::features::settings {
Settings current() {
    return g_settings;
}
void toggle(Control c) {
    g_settings = normalize(settings::toggle(g_settings, c));
}
void set_level(Control c, int v) {
    g_settings = normalize(settings::set_level(g_settings, c, v));
}
void set(const Settings& s) {
    g_settings = normalize(s);
}
}
namespace mosaic::features::led_feedback {
void acknowledge() { }
}

// ---- display ------------------------------------------------------------------------
namespace {
constexpr int W = 1280, H = 720;
std::vector<uint16_t> g_draw(W* H);
void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}
void read_encoder(lv_indev_t*, lv_indev_data_t* data) {
    data->enc_diff = static_cast<int16_t>(g_enc_diff);
    g_enc_diff = 0;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void read_touch(lv_indev_t*, lv_indev_data_t* data) {
    data->point = { static_cast<lv_coord_t>(g_touch_x), static_cast<lv_coord_t>(g_touch_y) };
    data->state = g_touch ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---- provisioning stand-in --------------------------------------------------------
prov::StatusScreen g_status_screen;
prov::SetupScreen g_setup_screen;
prov::ViewState g_view = prov::present(prov::StartupView::starting);
prov::ViewState g_rendered;
bool g_has_rendered = false;
bool g_qr = false;
bool g_qr_shown = false;
bool g_long_network = false;
bool g_setup_failure = false;
bool g_failure_shown = false;
bool setup_create(lv_obj_t* parent, void*) {
    if (!g_status_screen.create(parent))
        return false;
    g_setup_screen.create(parent);
    return true;
}
void setup_routine(void*) {
    if (g_qr != g_qr_shown) {
        g_qr_shown = g_qr;
        if (g_qr) {
            g_status_screen.hide();
            const char* ssid = g_long_network ? "Mosaic-4F2A1C-Long-Network-Name-For-Layout" : "Mosaic-4F2A1C";
            g_setup_screen.show(
                ssid, "kite-amber-42", "http://192.168.4.1/", "WIFI:T:WPA;S:Mosaic-4F2A1C;P:kite-amber-42;;");
            g_failure_shown = false;
        } else {
            g_setup_screen.hide();
            g_has_rendered = false;
        }
    }
    if (g_qr) {
        if (g_setup_failure == g_failure_shown)
            return;
        g_setup_screen.render_failure(g_setup_failure ? "Saving settings failed. Please try again." : "");
        g_failure_shown = g_setup_failure;
        return;
    }
    if (!g_has_rendered || !(g_rendered == g_view)) {
        g_status_screen.render(g_view);
        g_rendered = g_view;
        g_has_rendered = true;
    }
}
ui::navigation::Conditions conditions(void*) {
    return { .starting = g_view.starting && !g_qr, .online = g_view.online && !g_qr, .paired = g_paired };
}

ui::Renderer g_renderer;
void run(int ms) {
    for (int t = 0; t < ms; t += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
        g_renderer.routine(g_renderer.context);
    }
}
prov::ViewState view(prov::Phase phase, prov::VisibleReason reason = prov::VisibleReason::connecting,
    prov::RecoveryState recovery = prov::RecoveryState::idle) {
    prov::Snapshot s { };
    s.phase = phase;
    s.reason = reason;
    s.recovery = recovery;
    return prov::present(s);
}
}

int main(int argc, char** argv) {
    std::string shots = "build/preview-shots";
    std::string initial = "home";
    std::string screenshot_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--shots" && i + 1 < argc)
            shots = argv[++i];
        else if (arg == "--screenshot" && i + 1 < argc)
            screenshot_path = argv[++i];
        else if (arg == "--state" && i + 1 < argc)
            initial = argv[++i];
        else {
            std::fprintf(stderr, "Usage: preview [--state NAME] [--shots DIR] [--screenshot FILE.bmp]\n");
            return 2;
        }
    }
    setenv("TZ", "MST7", 1);
    tzset();
    lv_init();
    lv_display_t* display = lv_display_create(W, H);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(
        display, g_draw.data(), nullptr, g_draw.size() * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);
    g_encoder = lv_indev_create();
    lv_indev_set_type(g_encoder, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(g_encoder, read_encoder);
    auto* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_touch);
    ui::styles::initialize();

    auto* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(TOKEN_COLOR_BACKGROUND_DARK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, TOKEN_SPACE_8, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_renderer = ui::navigation::renderer({
        .setup = { setup_create, setup_routine, nullptr, nullptr, nullptr },
        .home = features::home::page(),
        .settings = features::settings::page(),
        .notification = { },
        .conditions = conditions,
        .conditions_context = nullptr,
    });
    if (!g_renderer.create(screen, nullptr)) {
        std::puts("create failed");
        return 1;
    }
    lv_screen_load(screen);

    {
        constexpr std::array kStates { "boot", "qr", "long-network", "setup-failure", "connecting", "offline",
            "network-unavailable", "startup-failure", "setup-opening", "setup-saved", "recovery-failure", "unpaired",
            "home", "trouble", "settings", "settings-manual", "settings-muted" };
        size_t state_index = 0;
        auto select = [&](const std::string& state) {
            bool known = false;
            for (size_t i = 0; i < kStates.size(); ++i) {
                if (state == kStates[i]) {
                    state_index = i;
                    known = true;
                    break;
                }
            }
            if (!known) {
                std::fprintf(stderr, "Unknown state: %s\n", state.c_str());
                std::exit(2);
            }
            ui::navigation::back();
            if (g_qr) {
                g_setup_screen.hide();
                g_qr_shown = false;
                g_has_rendered = false;
            }
            g_qr = false;
            g_long_network = false;
            g_setup_failure = false;
            g_paired = false;
            g_status = events::Status::disconnected;
            if (state == "boot")
                g_view = prov::present(prov::StartupView::starting);
            else if (state == "qr")
                g_qr = true;
            else if (state == "long-network") {
                g_long_network = true;
                g_qr = true;
            } else if (state == "setup-failure") {
                g_setup_failure = true;
                g_qr = true;
            } else if (state == "connecting")
                g_view = view(prov::Phase::starting_saved);
            else if (state == "offline")
                g_view = view(prov::Phase::offline, prov::VisibleReason::retrying);
            else if (state == "network-unavailable")
                g_view = view(prov::Phase::offline, prov::VisibleReason::unavailable);
            else if (state == "startup-failure")
                g_view = prov::present(prov::StartupView::unavailable);
            else if (state == "setup-opening")
                g_view = view(prov::Phase::requesting_setup);
            else if (state == "setup-saved")
                g_view = view(prov::Phase::saved_visible);
            else if (state == "recovery-failure")
                g_view = view(prov::Phase::recovery_failed, prov::VisibleReason::retrying, prov::RecoveryState::failed);
            else if (state == "unpaired")
                g_view = view(prov::Phase::online);
            else if (state == "home" || state == "trouble" || state == "settings" || state == "settings-manual"
                || state == "settings-muted") {
                g_view = view(prov::Phase::online);
                g_paired = true;
                g_status = state == "trouble" ? events::Status::error : events::Status::connected;
                auto settings = features::settings::Settings { };
                if (state == "settings-manual")
                    settings.auto_brightness = false;
                if (state == "settings-muted")
                    settings.muted = true;
                features::settings::set(settings);
                run(50);
                if (state.starts_with("settings"))
                    ui::navigation::open(ui::navigation::Screen::settings);
            }
            run(50);
        };
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            std::fprintf(stderr, "%s\n", SDL_GetError());
            return 1;
        }
        auto* window = SDL_CreateWindow("Mosaic device preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        auto* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
        if (window && !renderer)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
        if (!window || !renderer || !texture) {
            std::fprintf(stderr, "%s\n", SDL_GetError());
            return 1;
        }
        auto choose = [&](const std::string& state) {
            select(state);
            SDL_SetWindowTitle(window, ("Mosaic preview: " + state).c_str());
        };
        choose(initial);
        auto save_frame = [&](const std::filesystem::path& path) {
            auto* image = SDL_CreateRGBSurfaceWithFormatFrom(
                g_draw.data(), W, H, 16, W * sizeof(uint16_t), SDL_PIXELFORMAT_RGB565);
            const bool saved = image && SDL_SaveBMP(image, path.string().c_str()) == 0;
            if (!saved)
                std::fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
            else
                std::printf("Screenshot: %s\n", path.string().c_str());
            SDL_FreeSurface(image);
            return saved;
        };
        if (!screenshot_path.empty()) {
            run(300);
            const bool saved = save_frame(screenshot_path);
            SDL_DestroyTexture(texture);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return saved ? 0 : 1;
        }
        std::puts("Left/Right: turn, Enter/Space: press, Backspace: hold for Back, mouse: touch, [ ]: switch states, "
                  "F12: screenshot, Esc: "
                  "quit.\n"
                  "1 boot, 2 QR, 3 connecting, 4 offline, 5 unpaired, 6 Home, 7 trouble, 8 Settings, 9 long network, 0 "
                  "setup failure.\n"
                  "Other states: use --state NAME (see firmware/README.md).");
        bool running = true;
        int shot_number = 0;
        while (running) {
            bool screenshot = false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT)
                    running = false;
                if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    switch (event.key.keysym.sym) {
                    case SDLK_LEFT:
                        g_enc_diff = -1;
                        break;
                    case SDLK_RIGHT:
                        g_enc_diff = 1;
                        break;
                    case SDLK_RETURN:
                    case SDLK_SPACE:
                        g_pressed = true;
                        break;
                    case SDLK_BACKSPACE:
                        ++g_backs;
                        break;
                    case SDLK_1:
                        choose("boot");
                        break;
                    case SDLK_2:
                        choose("qr");
                        break;
                    case SDLK_3:
                        choose("connecting");
                        break;
                    case SDLK_4:
                        choose("offline");
                        break;
                    case SDLK_5:
                        choose("unpaired");
                        break;
                    case SDLK_6:
                        choose("home");
                        break;
                    case SDLK_7:
                        choose("trouble");
                        break;
                    case SDLK_8:
                        choose("settings");
                        break;
                    case SDLK_9:
                        choose("long-network");
                        break;
                    case SDLK_0:
                        choose("setup-failure");
                        break;
                    case SDLK_LEFTBRACKET:
                        choose(kStates[(state_index + kStates.size() - 1) % kStates.size()]);
                        break;
                    case SDLK_RIGHTBRACKET:
                        choose(kStates[(state_index + 1) % kStates.size()]);
                        break;
                    case SDLK_F12:
                        screenshot = true;
                        break;
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                    }
                }
                if (event.type == SDL_KEYUP
                    && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE))
                    g_pressed = false;
                if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP
                    || event.type == SDL_MOUSEMOTION) {
                    int ww, wh;
                    SDL_GetWindowSize(window, &ww, &wh);
                    const int x = event.type == SDL_MOUSEMOTION ? event.motion.x : event.button.x;
                    const int y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
                    g_touch_x = x * W / ww;
                    g_touch_y = y * H / wh;
                    if (event.type == SDL_MOUSEBUTTONDOWN)
                        g_touch = true;
                    if (event.type == SDL_MOUSEBUTTONUP)
                        g_touch = false;
                }
            }
            run(16);
            SDL_UpdateTexture(texture, nullptr, g_draw.data(), W * sizeof(uint16_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
            if (screenshot) {
                std::filesystem::create_directories(shots);
                char time_text[32];
                const auto now = std::time(nullptr);
                std::strftime(time_text, sizeof(time_text), "%Y%m%d-%H%M%S", std::localtime(&now));
                const auto path = std::filesystem::path(shots)
                    / (std::string("mosaic-") + time_text + "-" + std::to_string(++shot_number) + ".bmp");
                save_frame(path);
            }
            SDL_Delay(16);
        }
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
}
