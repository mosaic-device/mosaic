#include "host.hpp"
#include "../../drivers/encoder.hpp"
#include <array>
#include <initializer_list>
namespace mosaic::ui::navigation {
namespace {
    // Boot and setup share provisioning's container.
    enum Slot : uint8_t { kSetup, kHome, kSettings, kNotification, kSlots };
    struct Container {
        lv_obj_t* object = nullptr;
        lv_group_t* group = nullptr;
    };

    Screens s_screens;
    Navigator s_navigator;
    std::array<Container, kSlots> s_containers { };
    Slot s_shown = kSlots;
    // Setup credentials are rendering: provisioning's screen stays in front until its
    // reported state catches up and says Wi-Fi is no longer online.
    bool s_setup_forced = false;
    uint32_t s_backs_seen = 0;

    Slot slot(Screen screen) {
        switch (screen) {
        case Screen::boot:
        case Screen::setup:
            return kSetup;
        case Screen::home:
            return kHome;
        case Screen::settings:
            return kSettings;
        case Screen::notification:
            return kNotification;
        }
        return kSetup;
    }
    // Shows the active screen's container and gives the encoder its focus group,
    // which remembers the focused control from the last visit.
    void show_active() {
        const Slot active = slot(s_navigator.active());
        if (active == s_shown)
            return;
        for (size_t i = 0; i < kSlots; ++i) {
            if (!s_containers[i].object)
                continue;
            if (i == active)
                lv_obj_remove_flag(s_containers[i].object, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(s_containers[i].object, LV_OBJ_FLAG_HIDDEN);
        }
        drivers::encoder::set_group(s_containers[active].group);
        s_shown = active;
    }
    void follow_conditions() {
        Conditions conditions = s_screens.conditions(s_screens.conditions_context);
        if (s_setup_forced && !conditions.online)
            s_setup_forced = false;
        if (s_setup_forced)
            conditions.online = false;
        s_navigator.update(conditions);
    }
    bool create_container(lv_obj_t* root, Slot index, bool (*create)(lv_obj_t*, void*), void* context) {
        if (!create)
            return true;
        Container& container = s_containers[index];
        container.object = lv_obj_create(root);
        container.group = lv_group_create();
        if (!container.object || !container.group)
            return false;
        // Fill the root and center content, matching the layout screens had on the root.
        lv_obj_remove_style_all(container.object);
        lv_obj_set_size(container.object, lv_pct(100), lv_pct(100));
        lv_obj_set_flex_flow(container.object, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(container.object, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(container.object, LV_OBJ_FLAG_HIDDEN);
        lv_group_t* previous = lv_group_get_default();
        lv_group_set_default(container.group);
        const bool created = create(container.object, context);
        lv_group_set_default(previous);
        return created;
    }
    bool create(lv_obj_t* root, void*) {
        if (!create_container(root, kSetup, s_screens.setup.create, s_screens.setup.context)
            || !create_container(root, kHome, s_screens.home.create, s_screens.home.context)
            || !create_container(root, kSettings, s_screens.settings.create, s_screens.settings.context)
            || !create_container(root, kNotification, s_screens.notification.create, s_screens.notification.context))
            return false;
        s_backs_seen = drivers::encoder::back_presses();
        follow_conditions();
        show_active();
        return true;
    }
    void routine(void*) {
        s_screens.setup.routine(s_screens.setup.context);
        for (const Page* page : { &s_screens.home, &s_screens.settings, &s_screens.notification })
            if (page->update)
                page->update(page->context);
        follow_conditions();
        // Each Back press counts once; Back with nowhere to go does nothing.
        for (const uint32_t backs = drivers::encoder::back_presses(); s_backs_seen != backs; ++s_backs_seen)
            s_navigator.back();
        show_active();
    }
    bool credentials(const void* bytes, size_t size, void*) {
        s_setup_forced = true;
        s_navigator.update({ .starting = false, .online = false, .paired = false });
        show_active();
        return s_screens.setup.credentials(bytes, size, s_screens.setup.context);
    }
    void cancel_credentials(void*) {
        s_screens.setup.cancel_credentials(s_screens.setup.context);
    }
} // namespace
Renderer renderer(const Screens& screens) {
    s_screens = screens;
    return { create, routine, credentials, cancel_credentials, nullptr };
}
bool open(Screen screen) {
    if (!s_containers[slot(screen)].object || !s_navigator.open(screen))
        return false;
    show_active();
    return true;
}
bool back() {
    if (!s_navigator.back())
        return false;
    show_active();
    return true;
}
}
