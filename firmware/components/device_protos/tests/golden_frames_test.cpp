// Checks the nanopb bindings against platform/packages/protocols/testdata/frames.txt.
// The server's C# test checks the same file, so both sides agree on every byte.
// See the wiki's Device Connection Protocol and Notification Delivery Protocol.

#include "events.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using DeviceFrame = mosaic_device_v1_DeviceToBackendEnvelope;
using ServerFrame = mosaic_device_v1_BackendToDeviceEnvelope;

// Bytes 0x00..0x1F.
bool is_test_secret(const pb_byte_t (&secret)[32]) {
    for (size_t i = 0; i < sizeof secret; ++i) {
        if (secret[i] != i) {
            return false;
        }
    }
    return true;
}

int failures = 0;

void check(bool condition, const std::string& key, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s: %s\n", key.c_str(), what);
        ++failures;
    }
}

bool same(const char* actual, const char* expected) {
    return std::strcmp(actual, expected) == 0;
}

struct Frame {
    bool from_device;
    std::string kind;
    std::vector<uint8_t> bytes;
};

bool decode(const Frame& frame, DeviceFrame& device, ServerFrame& server) {
    pb_istream_t stream = pb_istream_from_buffer(frame.bytes.data(), frame.bytes.size());
    if (frame.from_device) {
        device = mosaic_device_v1_DeviceToBackendEnvelope_init_zero;
        return pb_decode(&stream, mosaic_device_v1_DeviceToBackendEnvelope_fields, &device);
    }
    server = mosaic_device_v1_BackendToDeviceEnvelope_init_zero;
    return pb_decode(&stream, mosaic_device_v1_BackendToDeviceEnvelope_fields, &server);
}

std::vector<uint8_t> encode(const Frame& frame, const DeviceFrame& device, const ServerFrame& server) {
    std::vector<uint8_t> out(frame.from_device ? mosaic_device_v1_DeviceToBackendEnvelope_size
                                               : mosaic_device_v1_BackendToDeviceEnvelope_size);
    pb_ostream_t stream = pb_ostream_from_buffer(out.data(), out.size());
    bool ok = frame.from_device ? pb_encode(&stream, mosaic_device_v1_DeviceToBackendEnvelope_fields, &device)
                                : pb_encode(&stream, mosaic_device_v1_BackendToDeviceEnvelope_fields, &server);
    if (!ok) {
        return { };
    }
    out.resize(stream.bytes_written);
    return out;
}

bool pending_k7qf2m9x(const ServerFrame& frame) {
    return frame.which_payload == mosaic_device_v1_BackendToDeviceEnvelope_pairing_pending_tag
        && same(frame.payload.pairing_pending.code, "K7QF2M9X")
        && frame.payload.pairing_pending.expires_in_seconds == 600;
}

bool rejected(const ServerFrame& frame, mosaic_device_v1_ConnectionRejectReason reason) {
    return frame.which_payload == mosaic_device_v1_BackendToDeviceEnvelope_rejected_tag
        && frame.payload.rejected.reason == reason;
}

// 2026-11-05 18:30 UTC.
constexpr int64_t k_test_occurred_at = 1793903400;

std::string repeat(const char* text, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        out += text;
    }
    return out;
}

bool test_notification(const ServerFrame& frame, mosaic_device_v1_NotificationPriority priority) {
    if (frame.which_payload != mosaic_device_v1_BackendToDeviceEnvelope_notification_tag) {
        return false;
    }
    const auto& notification = frame.payload.notification;
    return same(notification.id, "n-0001") && notification.priority == priority && same(notification.source, "Mosaic")
        && same(notification.title, "Test notification") && same(notification.text, "")
        && notification.occurred_at == k_test_occurred_at;
}

bool receipt(const DeviceFrame& frame, int status) {
    return frame.which_payload == mosaic_device_v1_DeviceToBackendEnvelope_notification_receipt_tag
        && same(frame.payload.notification_receipt.notification_id, "n-0001")
        && static_cast<int>(frame.payload.notification_receipt.status) == status;
}

// Expected field values for every "valid" frame, keyed by "<direction> <name>".
const std::map<std::string, bool (*)(const DeviceFrame&)> valid_device_frames = {
    { "d2s connect",
        [](const DeviceFrame& frame) {
            return frame.which_payload == mosaic_device_v1_DeviceToBackendEnvelope_connect_tag
                && is_test_secret(frame.payload.connect.auth_secret) && frame.payload.connect.protocol_version == 1;
        } },
    { "d2s receipt_stored",
        [](const DeviceFrame& frame) {
            return receipt(frame, mosaic_device_v1_NotificationReceiptStatus_NOTIFICATION_RECEIPT_STATUS_STORED);
        } },
    { "d2s receipt_duplicate",
        [](const DeviceFrame& frame) {
            return receipt(frame, mosaic_device_v1_NotificationReceiptStatus_NOTIFICATION_RECEIPT_STATUS_DUPLICATE);
        } },
    { "d2s receipt_invalid",
        [](const DeviceFrame& frame) {
            return receipt(frame, mosaic_device_v1_NotificationReceiptStatus_NOTIFICATION_RECEIPT_STATUS_INVALID);
        } },
    // A status this build doesn't know is kept, so it re-encodes unchanged.
    { "d2s receipt_unknown_status", [](const DeviceFrame& frame) { return receipt(frame, 7); } },
};

const std::map<std::string, bool (*)(const ServerFrame&)> valid_server_frames = {
    { "b2d paired",
        [](const ServerFrame& frame) {
            return frame.which_payload == mosaic_device_v1_BackendToDeviceEnvelope_paired_tag;
        } },
    { "b2d pairing_pending", pending_k7qf2m9x },
    { "b2d rejected_malformed_request",
        [](const ServerFrame& frame) {
            return rejected(frame, mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_MALFORMED_REQUEST);
        } },
    { "b2d rejected_unsupported_version",
        [](const ServerFrame& frame) {
            return rejected(
                frame, mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_UNSUPPORTED_VERSION);
        } },
    { "b2d rejected_credential_revoked",
        [](const ServerFrame& frame) {
            return rejected(frame, mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_CREDENTIAL_REVOKED);
        } },
    { "b2d rejected_server_busy",
        [](const ServerFrame& frame) {
            return rejected(frame, mosaic_device_v1_ConnectionRejectReason_CONNECTION_REJECT_REASON_SERVER_BUSY);
        } },
    { "b2d notification",
        [](const ServerFrame& frame) {
            return test_notification(frame, mosaic_device_v1_NotificationPriority_NOTIFICATION_PRIORITY_NORMAL);
        } },
    { "b2d notification_no_priority",
        [](const ServerFrame& frame) {
            return test_notification(frame, mosaic_device_v1_NotificationPriority_NOTIFICATION_PRIORITY_UNSPECIFIED);
        } },
    { "b2d notification_full",
        [](const ServerFrame& frame) {
            // Every string at its limit; the title is 63 two-byte characters and "!".
            const auto& notification = frame.payload.notification;
            return frame.which_payload == mosaic_device_v1_BackendToDeviceEnvelope_notification_tag
                && notification.id == repeat("I", 63)
                && notification.priority == mosaic_device_v1_NotificationPriority_NOTIFICATION_PRIORITY_HIGH
                && notification.source == repeat("S", 31) && notification.title == repeat("\xC3\xA9", 63) + "!"
                && notification.text == repeat("T", 1023) && notification.occurred_at == k_test_occurred_at;
        } },
};

// An envelope with an unknown payload decodes with no payload selected.
const std::set<std::string> ignored_frames = { "d2s unknown_payload", "b2d unknown_payload" };

const std::set<std::string> bad_length_frames = { "d2s connect_secret_long", "d2s connect_secret_short",
    "b2d pairing_pending_code_long", "b2d notification_title_long" };
const std::set<std::string> truncated_frames
    = { "d2s connect_truncated", "b2d pairing_pending_truncated", "b2d notification_truncated" };

std::map<std::string, Frame> load(const char* path) {
    std::map<std::string, Frame> frames;
    std::ifstream file(path);
    check(file.good(), path, "cannot open frames file");
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string direction, name, kind, hex, extra;
        fields >> direction >> name >> kind >> hex;
        bool well_formed
            = !hex.empty() && !(fields >> extra) && hex.size() % 2 == 0 && (direction == "d2s" || direction == "b2d");
        check(well_formed, line, "malformed frame line");
        if (!well_formed) {
            continue;
        }
        Frame frame { direction == "d2s", kind, { } };
        for (size_t i = 0; i < hex.size(); i += 2) {
            frame.bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        }
        check(frames.emplace(direction + " " + name, frame).second, line, "duplicate frame");
    }
    return frames;
}

void check_frame(const std::string& key, const Frame& frame) {
    DeviceFrame device;
    ServerFrame server;
    bool decoded = decode(frame, device, server);

    if (frame.kind == "valid") {
        check(decoded, key, "valid frame failed to decode");
        if (!decoded) {
            return;
        }
        if (frame.from_device) {
            auto expected = valid_device_frames.find(key);
            check(expected != valid_device_frames.end(), key, "no expected fields");
            check(expected != valid_device_frames.end() && expected->second(device), key, "fields differ");
        } else {
            auto expected = valid_server_frames.find(key);
            check(expected != valid_server_frames.end(), key, "no expected fields");
            check(expected != valid_server_frames.end() && expected->second(server), key, "fields differ");
        }
        check(encode(frame, device, server) == frame.bytes, key, "re-encoding changed the bytes");
    } else if (frame.kind == "ignored") {
        check(ignored_frames.contains(key), key, "unexpected ignored frame");
        check(decoded, key, "unknown payload must be skipped, not fail");
        if (decoded) {
            pb_size_t which = frame.from_device ? device.which_payload : server.which_payload;
            check(which == 0, key, "a payload was selected");
        }
    } else if (frame.kind == "bad_length") {
        check(bad_length_frames.contains(key), key, "unexpected bad_length frame");
        check(!decoded, key, "a field outside its nanopb limit must fail to decode");
    } else if (frame.kind == "truncated") {
        check(truncated_frames.contains(key), key, "unexpected truncated frame");
        check(!decoded, key, "a truncated frame must fail to decode");
    } else {
        check(false, key, "unknown frame kind");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <frames.txt>\n", argv[0]);
        return 2;
    }
    auto frames = load(argv[1]);

    std::set<std::string> expected;
    for (const auto& [key, _] : valid_device_frames) {
        expected.insert(key);
    }
    for (const auto& [key, _] : valid_server_frames) {
        expected.insert(key);
    }
    expected.insert(ignored_frames.begin(), ignored_frames.end());
    expected.insert(bad_length_frames.begin(), bad_length_frames.end());
    expected.insert(truncated_frames.begin(), truncated_frames.end());

    std::set<std::string> actual;
    for (const auto& [key, frame] : frames) {
        actual.insert(key);
        check_frame(key, frame);
    }
    check(actual == expected, "frames.txt", "fixture and expectations cover different frames");

    if (failures == 0) {
        std::printf("%zu golden frames decoded as expected\n", frames.size());
    }
    return failures == 0 ? 0 : 1;
}
