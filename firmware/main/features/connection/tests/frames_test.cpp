// Checks encode_connect and decode_reply against the shared golden frames in
// platform/packages/protocols/testdata/frames.txt, which the server's C# tests also use.
#include "../frames.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace mosaic::features::connection;
namespace notifications = mosaic::features::notifications;

namespace {
struct Golden {
    std::string direction;
    std::string kind;
    std::vector<uint8_t> bytes;
};

std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return bytes;
}

std::map<std::string, Golden> load(const char* path) {
    std::ifstream file(path);
    assert(file);
    std::map<std::string, Golden> frames;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        std::string direction, name, kind, hex;
        fields >> direction >> name >> kind >> hex;
        frames[direction + " " + name] = { direction, kind, from_hex(hex) };
    }
    return frames;
}

// The test secret from frames.txt: bytes 0x00..0x1F. Never a real credential.
std::array<uint8_t, kSecretSize> test_secret() {
    std::array<uint8_t, kSecretSize> secret;
    for (size_t i = 0; i < secret.size(); ++i)
        secret[i] = static_cast<uint8_t>(i);
    return secret;
}

void connect_matches_the_golden_frame(const std::map<std::string, Golden>& golden) {
    std::array<uint8_t, kSendLimit> out { };
    const size_t length = encode_connect(test_secret(), out);
    const std::vector<uint8_t>& expected = golden.at("d2s connect").bytes;
    assert(length == expected.size());
    assert(std::vector<uint8_t>(out.begin(), out.begin() + length) == expected);
}

void connect_needs_room() {
    std::array<uint8_t, 8> small { };
    assert(encode_connect(test_secret(), small) == 0);
}

// The golden frames' notifications.
notifications::Notification test_notification() {
    notifications::Notification n;
    notifications::copy_text(n.id, "n-0001");
    notifications::copy_text(n.source, "Mosaic");
    notifications::copy_text(n.title, "Test notification");
    n.occurred_at = 1'793'903'400;
    return n;
}
notifications::Notification full_notification() {
    notifications::Notification n;
    notifications::copy_text(n.id, std::string(63, 'I'));
    n.priority = notifications::Priority::high;
    notifications::copy_text(n.source, std::string(31, 'S'));
    std::string title;
    for (int i = 0; i < 63; ++i)
        title += "\xC3\xA9";
    notifications::copy_text(n.title, title + "!");
    notifications::copy_text(n.text, std::string(1023, 'T'));
    n.occurred_at = 1'793'903'400;
    return n;
}

void receipts_match_the_golden_frames(const std::map<std::string, Golden>& golden) {
    const std::pair<const char*, notifications::Receipt> receipts[] = {
        { "d2s receipt_stored", notifications::Receipt::stored },
        { "d2s receipt_duplicate", notifications::Receipt::duplicate },
        { "d2s receipt_invalid", notifications::Receipt::invalid },
    };
    for (const auto& [name, receipt] : receipts) {
        std::array<uint8_t, kSendLimit> out { };
        const size_t length = encode_receipt("n-0001", receipt, out);
        assert(std::vector<uint8_t>(out.begin(), out.begin() + length) == golden.at(name).bytes);
    }
    // An ID over its limit can't be confirmed.
    std::array<uint8_t, kSendLimit> out { };
    assert(encode_receipt(std::string(64, 'I'), notifications::Receipt::stored, out) == 0);
}

bool same(Reply actual, Reply expected) {
    if (actual.frame != expected.frame)
        return false;
    if (actual.frame == Frame::pairing_pending)
        return actual.code == expected.code && actual.expires_in_s == expected.expires_in_s;
    if (actual.frame == Frame::notification) {
        const auto& a = actual.notification;
        const auto& e = expected.notification;
        return a.id_view() == e.id_view() && a.priority == e.priority && a.source_view() == e.source_view()
            && a.title_view() == e.title_view() && a.text_view() == e.text_view() && a.occurred_at == e.occurred_at;
    }
    return actual.frame != Frame::rejected || actual.reason == expected.reason;
}

void every_server_frame_decodes_as_expected(const std::map<std::string, Golden>& golden) {
    // The handshake replies. Every other server frame in the file, valid or not, is `other`,
    // so a frame added later for another feature needs no change here.
    const std::map<std::string, Reply> replies = {
        { "b2d paired", { .frame = Frame::paired } },
        { "b2d pairing_pending",
            { .frame = Frame::pairing_pending,
                .code = { 'K', '7', 'Q', 'F', '2', 'M', '9', 'X' },
                .expires_in_s = 600 } },
        { "b2d rejected_malformed_request", { .frame = Frame::rejected, .reason = Reject::malformed_request } },
        { "b2d rejected_unsupported_version", { .frame = Frame::rejected, .reason = Reject::unsupported_version } },
        { "b2d rejected_credential_revoked", { .frame = Frame::rejected, .reason = Reject::credential_revoked } },
        { "b2d rejected_server_busy", { .frame = Frame::rejected, .reason = Reject::server_busy } },
        { "b2d notification", { .frame = Frame::notification, .notification = test_notification() } },
        { "b2d notification_no_priority", { .frame = Frame::notification, .notification = test_notification() } },
        { "b2d notification_full", { .frame = Frame::notification, .notification = full_notification() } },
    };
    size_t checked = 0;
    for (const auto& [name, frame] : golden) {
        if (frame.direction != "b2d")
            continue;
        const auto expected = replies.find(name);
        const Reply wanted = expected != replies.end() ? expected->second : Reply { };
        if (!same(decode_reply(frame.bytes), wanted)) {
            std::fprintf(stderr, "FAIL %s (%s)\n", name.c_str(), frame.kind.c_str());
            assert(false);
        }
        // A handshake reply must be a valid frame in the file.
        assert(expected == replies.end() || frame.kind == "valid");
        ++checked;
    }
    // Every handshake reply is still in the file.
    for (const auto& [name, reply] : replies)
        assert(golden.contains(name));
    assert(checked >= replies.size());
}

void unknown_and_unspecified_reasons_count_as_busy() {
    // rejected with reason 0 (unspecified), then with an unknown reason 9.
    const std::vector<uint8_t> unspecified = { 0x1A, 0x00 };
    const std::vector<uint8_t> unknown = { 0x1A, 0x02, 0x08, 0x09 };
    assert(same(decode_reply(unspecified), { .frame = Frame::rejected, .reason = Reject::server_busy }));
    assert(same(decode_reply(unknown), { .frame = Frame::rejected, .reason = Reject::server_busy }));
}

void empty_and_oversized_frames_are_other() {
    assert(decode_reply({ }).frame == Frame::other);
    // A valid paired frame padded past the receive limit is refused before decoding.
    std::vector<uint8_t> oversized(kReceiveLimit + 1, 0);
    oversized[0] = 0x12;
    oversized[1] = 0x00;
    assert(decode_reply(oversized).frame == Frame::other);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto golden = load(argv[1]);
    connect_matches_the_golden_frame(golden);
    connect_needs_room();
    every_server_frame_decodes_as_expected(golden);
    receipts_match_the_golden_frames(golden);
    unknown_and_unspecified_reasons_count_as_busy();
    empty_and_oversized_frames_are_other();
    std::puts("Frames tests passed");
}
