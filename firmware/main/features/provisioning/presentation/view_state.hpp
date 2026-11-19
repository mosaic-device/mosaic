#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace mosaic::features::provisioning {
enum class ScreenKind : uint8_t { status, setup };
// Owned bounded text: snapshots never contain pointers to request buffers.
template <size_t Capacity> class ViewText {
public:
    constexpr ViewText(std::string_view text = { }) {
        *this = text;
    }
    constexpr ViewText& operator=(std::string_view text) {
        const size_t count = text.size() < Capacity ? text.size() : Capacity;
        for (size_t i = 0; i < count; ++i)
            bytes_[i] = text[i];
        bytes_[count] = '\0';
        return *this;
    }
    const char* data() const {
        return bytes_.data();
    }
    std::string_view view() const {
        return bytes_.data();
    }
    bool empty() const {
        return bytes_[0] == '\0';
    }
    bool operator==(const ViewText& other) const {
        return view() == other.view();
    }
    bool operator==(std::string_view other) const {
        return view() == other;
    }

private:
    std::array<char, Capacity + 1> bytes_ { };
};
struct ViewState {
    ScreenKind screen = ScreenKind::status;
    ViewText<32> heading;
    ViewText<192> message;
    ViewText<64> recovery_prompt;
    // The pairing code as XXXX-XXXX, shown large between the heading and the message. Empty
    // when there's no code to show.
    ViewText<9> code;
    bool recovery_visible = false;
    bool recovery_enabled = true;
    ViewText<192> setup_failure;
    uint32_t session = 0;
    // For navigation: startup has not reported a state yet, or Wi-Fi is joined with
    // setup closed.
    bool starting = false;
    bool online = false;
    // Setup is open, opening or closing, so it takes the screen even on a paired device.
    bool setup = false;
    bool operator==(const ViewState&) const = default;
};
enum class StartupView { starting, unavailable };
}
