// Checks the nanopb bindings against platform/packages/protocols/testdata/frames.txt.
// The server's C# test checks the same file, so both sides agree on every byte.
// See the wiki's Device Connection Protocol, section 9.

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

constexpr const char* auth_secret = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
constexpr const char* mac_address = "AA:BB:CC:DD:EE:FF";

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
    return frame.which_payload == mosaic_device_v1_BackendToDeviceEnvelope_rejected_tag && frame.request_id[0] == '\0'
        && frame.payload.rejected.reason == reason;
}

// Expected field values for every "valid" frame, keyed by "<direction> <name>".
const std::map<std::string, bool (*)(const DeviceFrame&)> valid_device_frames = {
    { "d2s connect",
        [](const DeviceFrame& frame) {
            return frame.which_payload == mosaic_device_v1_DeviceToBackendEnvelope_connect_tag
                && same(frame.request_id, "req-0001") && same(frame.payload.connect.mac_address, mac_address)
                && same(frame.payload.connect.auth_secret, auth_secret) && frame.payload.connect.protocol_version == 1;
        } },
};

const std::map<std::string, bool (*)(const ServerFrame&)> valid_server_frames = {
    { "b2d paired",
        [](const ServerFrame& frame) {
            return frame.which_payload == mosaic_device_v1_BackendToDeviceEnvelope_paired_tag
                && frame.request_id[0] == '\0';
        } },
    { "b2d paired_reply",
        [](const ServerFrame& frame) {
            return frame.which_payload == mosaic_device_v1_BackendToDeviceEnvelope_paired_tag
                && same(frame.request_id, "req-0001");
        } },
    { "b2d pairing_pending",
        [](const ServerFrame& frame) { return pending_k7qf2m9x(frame) && frame.request_id[0] == '\0'; } },
    { "b2d pairing_pending_reply",
        [](const ServerFrame& frame) { return pending_k7qf2m9x(frame) && same(frame.request_id, "req-0001"); } },
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
};

// An envelope with an unknown payload decodes with no payload and keeps its request_id.
const std::map<std::string, const char*> ignored_frames = {
    { "d2s unknown_payload", "req-0002" },
    { "b2d unknown_payload", "req-0005" },
};

const std::set<std::string> over_limit_frames
    = { "d2s connect_secret_over_limit", "b2d pairing_pending_code_over_limit" };
const std::set<std::string> truncated_frames = { "d2s connect_truncated", "b2d pairing_pending_truncated" };

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
        auto expected = ignored_frames.find(key);
        check(decoded, key, "unknown payload must be skipped, not fail");
        check(expected != ignored_frames.end(), key, "no expected request_id");
        if (decoded && expected != ignored_frames.end()) {
            const char* request_id = frame.from_device ? device.request_id : server.request_id;
            pb_size_t which = frame.from_device ? device.which_payload : server.which_payload;
            check(same(request_id, expected->second), key, "request_id not kept");
            check(which == 0, key, "a payload was selected");
        }
    } else if (frame.kind == "over_limit") {
        check(over_limit_frames.contains(key), key, "unexpected over_limit frame");
        check(!decoded, key, "a field over its max_size must fail to decode");
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
    for (const auto& [key, _] : ignored_frames) {
        expected.insert(key);
    }
    expected.insert(over_limit_frames.begin(), over_limit_frames.end());
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
