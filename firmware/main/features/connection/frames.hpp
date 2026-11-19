#pragma once

#include "policy.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace mosaic::features::connection {

// The wire protocol version this firmware speaks (protocol section 7).
inline constexpr uint32_t kProtocolVersion = 1;
// connect carries exactly this many secret bytes; the server rejects any other length.
inline constexpr size_t kSecretSize = 32;
// Fixed frame limits from protocol section 1. They never go down.
inline constexpr size_t kSendLimit = 512;
inline constexpr size_t kReceiveLimit = 4'096;

// A server frame, as far as the connection needs to know. `reason` matters only for
// Frame::rejected.
struct Reply {
    Frame frame = Frame::other;
    Reject reason = Reject::server_busy;
};

// Writes a connect envelope with kProtocolVersion and `secret` to `out`, which needs room
// for kSendLimit bytes. Returns the length, or 0 if it doesn't fit. Where the secret comes
// from, and keeping it, is the caller's job. Never log `secret` or the encoded frame.
size_t encode_connect(std::span<const uint8_t, kSecretSize> secret, std::span<uint8_t> out);

// Reads one server envelope. Anything that isn't a handshake reply comes back as
// Frame::other: application frames such as notifications, an empty or unknown payload,
// and frames that fail to decode, including ones over kReceiveLimit.
Reply decode_reply(std::span<const uint8_t> frame);

} // namespace mosaic::features::connection
