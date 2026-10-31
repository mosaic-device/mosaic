#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mosaic::ui::navigation {

enum class Screen : uint8_t {
    boot,
    // Provisioning's screens: setup, connecting, offline and unpaired.
    setup,
    home,
    settings,
    // Reserved for M3: shown above Home or Settings, and Back returns underneath.
    notification,
};

// What the rest of the device reports. Decides the base screen under everything else.
struct Conditions {
    // Provisioning has not reported its first state yet.
    bool starting = true;
    // Wi-Fi joined and setup is not in progress.
    bool online = false;
    // Paired and connected to the server.
    bool paired = false;
};

// The device's screen map: which screen shows, what can be opened from it, and what
// Back does. Home appears only once paired; until then the unpaired state is
// provisioning's "Wi-Fi connected" screen. The rest is provisional until #56.
class Navigator {
public:
    Screen active() const;
    // Call whenever the conditions may have changed. A new base screen closes
    // anything opened above the old one.
    void update(Conditions conditions);
    // Opens a screen above the active one. False when the map does not allow it
    // from here, and nothing changes.
    bool open(Screen screen);
    // Returns to the screen underneath. False on a base screen, where Back does nothing.
    bool back();

private:
    static constexpr size_t kDepth = 4;
    std::array<Screen, kDepth> stack_ { Screen::boot };
    size_t size_ = 1;
};

}
