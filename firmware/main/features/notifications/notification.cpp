#include "notification.hpp"

namespace mosaic::features::notifications {
namespace {
    template <size_t N> bool ends_within(const std::array<char, N>& field) {
        for (const char c : field)
            if (c == '\0')
                return true;
        return false;
    }
} // namespace

bool valid(const Notification& n) {
    if (!ends_within(n.id) || !ends_within(n.source) || !ends_within(n.title) || !ends_within(n.text))
        return false;
    const std::string_view id = n.id_view();
    if (id.empty())
        return false;
    for (const char c : id)
        if (c < 0x20 || c > 0x7E)
            return false;
    return !n.source_view().empty() && !n.title_view().empty() && n.occurred_at > 0;
}

} // namespace mosaic::features::notifications
