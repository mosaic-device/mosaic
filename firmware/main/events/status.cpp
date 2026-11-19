#include "status.hpp"

#include <array>
#include <utility>

namespace mosaic::events {
namespace {
    constexpr std::array<std::pair<Status, const char*>, 5> kNames { {
        { Status::disconnected, "disconnected" },
        { Status::pairing, "pairing" },
        { Status::connected, "connected" },
        { Status::error, "error" },
        { Status::update_required, "update" },
    } };
} // namespace

const char* name(Status status) {
    for (const auto& [value, text] : kNames)
        if (value == status)
            return text;
    return "unknown";
}

bool parse(std::string_view text, Status& status) {
    for (const auto& [value, name] : kNames) {
        if (text == name) {
            status = value;
            return true;
        }
    }
    return false;
}

} // namespace mosaic::events
