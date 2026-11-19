#include "credential.hpp"

#include <algorithm>

namespace mosaic::features::connection {

bool belongs_to(const Credential& saved, std::string_view url) {
    return !url.empty() && saved.server() == url;
}

bool fresh(std::span<const uint8_t, kSecretSize> secret, std::string_view url, Credential& out) {
    out = { };
    if (url.empty() || url.size() >= kUrlCapacity)
        return false;
    std::ranges::copy(secret, out.secret.begin());
    std::ranges::copy(url, out.url.begin());
    return true;
}

Change after(const Credential& saved, const Reply& reply) {
    switch (reply.frame) {
    case Frame::paired:
        return saved.paired ? Change::none : Change::mark_paired;
    case Frame::pairing_pending:
        return saved.paired ? Change::mark_unpaired : Change::none;
    case Frame::rejected:
        return reply.reason == Reject::credential_revoked ? Change::replace : Change::none;
    case Frame::notification:
    case Frame::other:
        break;
    }
    return Change::none;
}

} // namespace mosaic::features::connection
