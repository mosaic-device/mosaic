#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mosaic::features::notifications {

// HIGH breaks through focus mode once it exists; anything else counts as NORMAL.
enum class Priority : uint8_t { normal, high };

// One notification from the server, with the Notification Delivery Protocol's limits. The
// strings are UTF-8 and end within their arrays.
struct Notification {
    std::array<char, 64> id { };
    Priority priority = Priority::normal;
    // A display label such as "GitHub". Never branched on.
    std::array<char, 32> source { };
    std::array<char, 128> title { };
    std::array<char, 1024> text { };
    // When it happened, in Unix seconds.
    int64_t occurred_at = 0;

    std::string_view id_view() const {
        return view(id);
    }
    std::string_view source_view() const {
        return view(source);
    }
    std::string_view title_view() const {
        return view(title);
    }
    std::string_view text_view() const {
        return view(text);
    }

private:
    template <size_t N> static std::string_view view(const std::array<char, N>& field) {
        size_t length = 0;
        while (length < N && field[length] != '\0')
            ++length;
        return { field.data(), length };
    }
};

// What the device tells the server about a notification it decoded.
enum class Receipt : uint8_t {
    // New, and now in the inbox.
    stored,
    // The inbox already held one with this ID; nothing changed.
    duplicate,
    // A field breaks the protocol's rules; not kept, and the server won't resend it.
    invalid,
};

// The protocol's field rules: an ID of 1–63 printable ASCII characters, a non-empty source
// and title, a real time, and every string ending within its array.
bool valid(const Notification& notification);

// Copies `text` into `field`, cut at its limit without splitting a UTF-8 character.
template <size_t N> void copy_text(std::array<char, N>& field, std::string_view text) {
    size_t length = text.size() < N ? text.size() : N - 1;
    // Back off any continuation bytes, so a cut never leaves half a character.
    if (length < text.size())
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80)
            --length;
    field = { };
    for (size_t i = 0; i < length; ++i)
        field[i] = text[i];
}

} // namespace mosaic::features::notifications
