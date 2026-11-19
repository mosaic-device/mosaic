#include "frames.hpp"

#include "events.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <algorithm>

namespace mosaic::features::connection {

// Protocol section 6: every generated message must fit its frame limit, so a schema change
// that outgrows one fails the build instead of being cut short on the wire.
static_assert(mosaic_device_v1_DeviceToBackendEnvelope_size <= kSendLimit);
static_assert(mosaic_device_v1_BackendToDeviceEnvelope_size <= kReceiveLimit);
static_assert(sizeof(mosaic_device_v1_DeviceConnect::auth_secret) == kSecretSize);

namespace {
    Reject reject_reason(mosaic_device_v1_ConnectionRejectReason reason) {
        switch (reason) {
        case mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_MALFORMED_REQUEST:
            return Reject::malformed_request;
        case mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_UNSUPPORTED_VERSION:
            return Reject::unsupported_version;
        case mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_CREDENTIAL_REVOKED:
            return Reject::credential_revoked;
        case mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_SERVER_BUSY:
        case mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_UNSPECIFIED:
            break;
        }
        // Unspecified, or a reason newer than this firmware (protocol section 4).
        return Reject::server_busy;
    }
} // namespace

size_t encode_connect(std::span<const uint8_t, kSecretSize> secret, std::span<uint8_t> out) {
    mosaic_device_v1_DeviceToBackendEnvelope envelope = mosaic_device_v1_DeviceToBackendEnvelope_init_zero;
    envelope.which_payload = mosaic_device_v1_DeviceToBackendEnvelope_connect_tag;
    envelope.payload.connect.protocol_version = kProtocolVersion;
    std::ranges::copy(secret, envelope.payload.connect.auth_secret);
    pb_ostream_t stream = pb_ostream_from_buffer(out.data(), out.size());
    const bool ok = pb_encode(&stream, mosaic_device_v1_DeviceToBackendEnvelope_fields, &envelope);
    // The envelope holds a copy of the secret; don't leave it on the stack.
    std::ranges::fill(envelope.payload.connect.auth_secret, uint8_t { 0 });
    return ok ? stream.bytes_written : 0;
}

Reply decode_reply(std::span<const uint8_t> frame) {
    if (frame.size() > kReceiveLimit)
        return { };
    // About 1.3 KB, mostly room for a notification; the caller's stack must allow for it.
    mosaic_device_v1_BackendToDeviceEnvelope envelope = mosaic_device_v1_BackendToDeviceEnvelope_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(frame.data(), frame.size());
    if (!pb_decode(&stream, mosaic_device_v1_BackendToDeviceEnvelope_fields, &envelope))
        return { };
    switch (envelope.which_payload) {
    case mosaic_device_v1_BackendToDeviceEnvelope_paired_tag:
        return { .frame = Frame::paired };
    case mosaic_device_v1_BackendToDeviceEnvelope_pairing_pending_tag:
        // The code and its time left are for the pairing screen (#111).
        return { .frame = Frame::pairing_pending };
    case mosaic_device_v1_BackendToDeviceEnvelope_rejected_tag:
        return { .frame = Frame::rejected, .reason = reject_reason(envelope.payload.rejected.reason) };
    default:
        // Notifications (#112), and payloads this firmware doesn't know.
        return { };
    }
}

} // namespace mosaic::features::connection
