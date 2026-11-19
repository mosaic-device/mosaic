#pragma once

#include "frames.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mosaic::features::connection {

// The server URL as saved in setup, with room for its terminator.
inline constexpr size_t kUrlCapacity = 193;

// What the device keeps to prove who it is to one server: a random secret, the server URL it
// belongs to, and whether that server has paired it. Never log the secret.
struct Credential {
    std::array<uint8_t, kSecretSize> secret { };
    std::array<char, kUrlCapacity> url { };
    bool paired = false;

    std::string_view server() const {
        return url.data();
    }
    bool operator==(const Credential&) const = default;
};

// Whether `saved` is the credential to use for `url`. A credential belongs to the URL exactly as
// it was entered: setup recovery with the same URL keeps the pairing, and a changed URL needs a
// new credential and a new claim.
bool belongs_to(const Credential& saved, std::string_view url);

// A new, unpaired credential for `url` with `secret`, which must come from a CSPRNG. False if
// the URL doesn't fit; `out` is then left empty.
bool fresh(std::span<const uint8_t, kSecretSize> secret, std::string_view url, Credential& out);

// How a server reply changes the saved credential.
enum class Change : uint8_t {
    none,
    // The server paired this credential: remember it.
    mark_paired,
    // The server doesn't know this credential as paired: forget the pairing, keep the secret.
    mark_unpaired,
    // The server removed this device: replace the credential before connecting again.
    replace,
};
Change after(const Credential& saved, const Reply& reply);

} // namespace mosaic::features::connection
