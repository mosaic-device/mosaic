#include "../credential.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <string>

using namespace mosaic::features::connection;

namespace {
constexpr std::array<uint8_t, kSecretSize> kSecret { 1, 2, 3, 4 };
constexpr char kUrl[] = "wss://mosaic.example.ts.net/ws/device";

Credential made(std::string_view url = kUrl) {
    Credential credential;
    assert(fresh(kSecret, url, credential));
    return credential;
}

void a_fresh_credential_is_unpaired_and_belongs_to_its_url() {
    const Credential credential = made();
    assert(credential.secret == kSecret);
    assert(credential.server() == kUrl);
    assert(!credential.paired);
    assert(belongs_to(credential, kUrl));
}

void only_the_same_url_reuses_it() {
    const Credential credential = made();
    // Exactly as entered: a different path, port, case or trailing slash is another server.
    for (const char* other : { "wss://mosaic.example.ts.net/ws/device/", "wss://mosaic.example.ts.net:443/ws/device",
             "wss://Mosaic.example.ts.net/ws/device", "wss://other.example/ws/device", "" })
        assert(!belongs_to(credential, other));
    // Nothing saved yet: an empty credential belongs to no URL.
    assert(!belongs_to(Credential { }, kUrl));
    assert(!belongs_to(Credential { }, ""));
}

void a_url_that_does_not_fit_is_refused() {
    Credential credential = made();
    assert(fresh(kSecret, std::string(kUrlCapacity - 1, 'u'), credential));
    assert(!fresh(kSecret, std::string(kUrlCapacity, 'u'), credential));
    assert(credential == Credential { });
    assert(!fresh(kSecret, "", credential));
}

void replies_change_the_pairing() {
    Credential unpaired = made();
    Credential paired = made();
    paired.paired = true;
    const Reply accepted { .frame = Frame::paired };
    const Reply pending { .frame = Frame::pairing_pending };
    assert(after(unpaired, accepted) == Change::mark_paired);
    assert(after(paired, accepted) == Change::none);
    // A paired device the server no longer knows goes back to waiting for a claim.
    assert(after(paired, pending) == Change::mark_unpaired);
    assert(after(unpaired, pending) == Change::none);
    // Removed in the admin app: start over with a new secret.
    for (const Credential& saved : { unpaired, paired })
        assert(after(saved, { .frame = Frame::rejected, .reason = Reject::credential_revoked }) == Change::replace);
    // Other rejections and frames leave it alone.
    for (const Reject reason : { Reject::malformed_request, Reject::unsupported_version, Reject::server_busy })
        assert(after(paired, { .frame = Frame::rejected, .reason = reason }) == Change::none);
    assert(after(paired, { }) == Change::none);
}
} // namespace

int main() {
    a_fresh_credential_is_unpaired_and_belongs_to_its_url();
    only_the_same_url_reuses_it();
    a_url_that_does_not_fit_is_refused();
    replies_change_the_pairing();
    std::puts("Credential tests passed");
}
