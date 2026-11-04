using Google.Protobuf;
using Google.Protobuf.Reflection;
using Mosaic.Device.V1;

namespace Mosaic.Server.Tests;

// Checks the C# bindings against platform/packages/protocols/testdata/frames.txt.
// The firmware host test checks the same file with nanopb, so both sides agree on
// every byte. See the wiki's Device Connection Protocol, section 9.
public sealed class DeviceProtocolGoldenFrameTests
{
    private const string AuthSecret = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
    private const string MacAddress = "AA:BB:CC:DD:EE:FF";

    private static readonly IReadOnlyDictionary<string, IMessage> ValidDeviceFrames = new Dictionary<string, IMessage>
    {
        ["connect"] = new DeviceToBackendEnvelope
        {
            RequestId = "req-0001",
            Connect = new DeviceConnect { MacAddress = MacAddress, AuthSecret = AuthSecret, ProtocolVersion = 1 },
        },
    };

    private static readonly IReadOnlyDictionary<string, IMessage> ValidServerFrames = new Dictionary<string, IMessage>
    {
        ["paired"] = new BackendToDeviceEnvelope { Paired = new DevicePaired() },
        ["paired_reply"] = new BackendToDeviceEnvelope { RequestId = "req-0001", Paired = new DevicePaired() },
        ["pairing_pending"] = new BackendToDeviceEnvelope { PairingPending = Pending() },
        ["pairing_pending_reply"] = new BackendToDeviceEnvelope { RequestId = "req-0001", PairingPending = Pending() },
        ["rejected_malformed_request"] = Rejected(ConnectionRejectReason.MalformedRequest),
        ["rejected_unsupported_version"] = Rejected(ConnectionRejectReason.UnsupportedVersion),
        ["rejected_credential_revoked"] = Rejected(ConnectionRejectReason.CredentialRevoked),
        ["rejected_server_busy"] = Rejected(ConnectionRejectReason.ServerBusy),
    };

    private static readonly IReadOnlyDictionary<string, string> IgnoredFrames = new Dictionary<string, string>
    {
        ["d2s unknown_payload"] = "req-0002",
        ["b2d unknown_payload"] = "req-0005",
    };

    private static readonly IReadOnlyDictionary<string, (FieldDescriptor Field, Func<IMessage, string> Value)> OverLimitFrames =
        new Dictionary<string, (FieldDescriptor, Func<IMessage, string>)>
        {
            ["d2s connect_secret_over_limit"] = (
                DeviceConnect.Descriptor.FindFieldByName("auth_secret"),
                message => ((DeviceToBackendEnvelope)message).Connect.AuthSecret),
            ["b2d pairing_pending_code_over_limit"] = (
                PairingPending.Descriptor.FindFieldByName("code"),
                message => ((BackendToDeviceEnvelope)message).PairingPending.Code),
        };

    private static readonly string[] TruncatedFrames = ["d2s connect_truncated", "b2d pairing_pending_truncated"];

    public static TheoryData<string> FrameKeys => [.. Frames.Value.Keys];

    private static readonly Lazy<IReadOnlyDictionary<string, Frame>> Frames = new(LoadFrames);

    [Fact]
    public void FixtureAndExpectationsCoverTheSameFrames()
    {
        var expected = ValidDeviceFrames.Keys.Select(name => $"d2s {name}")
            .Concat(ValidServerFrames.Keys.Select(name => $"b2d {name}"))
            .Concat(IgnoredFrames.Keys)
            .Concat(OverLimitFrames.Keys)
            .Concat(TruncatedFrames)
            .Order(StringComparer.Ordinal);

        Assert.Equal(expected, Frames.Value.Keys.Order(StringComparer.Ordinal));
    }

    [Fact]
    public void FixtureCoversEveryRejectReasonAndHandshakeReply()
    {
        var reasons = ValidServerFrames.Values.Cast<BackendToDeviceEnvelope>()
            .Where(frame => frame.PayloadCase == BackendToDeviceEnvelope.PayloadOneofCase.Rejected)
            .Select(frame => frame.Rejected.Reason);
        var expectedReasons = Enum.GetValues<ConnectionRejectReason>().Where(reason => reason != ConnectionRejectReason.Unspecified);
        Assert.Equal(expectedReasons.Order(), reasons.Order());

        var cases = ValidServerFrames.Values.Cast<BackendToDeviceEnvelope>().Select(frame => frame.PayloadCase).ToHashSet();
        Assert.Contains(BackendToDeviceEnvelope.PayloadOneofCase.Paired, cases);
        Assert.Contains(BackendToDeviceEnvelope.PayloadOneofCase.PairingPending, cases);
    }

    [Theory]
    [MemberData(nameof(FrameKeys))]
    public void FrameDecodesAsItsKindRequires(string key)
    {
        var frame = Frames.Value[key];

        switch (frame.Kind)
        {
            case "valid":
                var expected = (frame.FromDevice ? ValidDeviceFrames : ValidServerFrames)[frame.Name];
                var decoded = Parse(frame);
                Assert.Equal(expected, decoded);
                Assert.Equal(frame.Bytes, decoded.ToByteArray());
                Assert.Equal(frame.Bytes, expected.ToByteArray());
                Assert.True(frame.Bytes.Length <= MaxFrameBytes(frame.FromDevice));
                break;

            case "ignored":
                var ignored = Parse(frame);
                Assert.Equal(IgnoredFrames[key], RequestId(ignored));
                Assert.Equal(0, PayloadCase(ignored));
                break;

            case "over_limit":
                // The C# bindings enforce no nanopb limits, so this decodes. The server
                // must reject it by comparing the field with its declared max_size.
                var (field, value) = OverLimitFrames[key];
                var oversized = Parse(frame);
                Assert.True(value(oversized).Length > UsableLength(field));
                break;

            case "truncated":
                Assert.Throws<InvalidProtocolBufferException>(() => Parse(frame));
                break;

            default:
                Assert.Fail($"Unknown frame kind '{frame.Kind}' for {key}.");
                break;
        }
    }

    [Fact]
    public void NanopbLimitsAreReadableFromTheBindings()
    {
        // The server's field checks (#103) can read limits from the descriptors
        // instead of copying them. A nanopb max_size includes the terminating NUL.
        Assert.Equal(63, UsableLength(DeviceConnect.Descriptor.FindFieldByName("auth_secret")));
        Assert.Equal(17, UsableLength(DeviceConnect.Descriptor.FindFieldByName("mac_address")));
        Assert.Equal(8, UsableLength(PairingPending.Descriptor.FindFieldByName("code")));
        Assert.Equal(63, UsableLength(DeviceToBackendEnvelope.Descriptor.FindFieldByName("request_id")));
    }

    private static PairingPending Pending() => new() { Code = "K7QF2M9X", ExpiresInSeconds = 600 };

    private static BackendToDeviceEnvelope Rejected(ConnectionRejectReason reason) =>
        new() { Rejected = new ConnectionRejected { Reason = reason } };

    private static IMessage Parse(Frame frame) => frame.FromDevice
        ? DeviceToBackendEnvelope.Parser.ParseFrom(frame.Bytes)
        : BackendToDeviceEnvelope.Parser.ParseFrom(frame.Bytes);

    private static string RequestId(IMessage message) => message switch
    {
        DeviceToBackendEnvelope device => device.RequestId,
        BackendToDeviceEnvelope server => server.RequestId,
        _ => throw new ArgumentOutOfRangeException(nameof(message)),
    };

    private static int PayloadCase(IMessage message) => message switch
    {
        DeviceToBackendEnvelope device => (int)device.PayloadCase,
        BackendToDeviceEnvelope server => (int)server.PayloadCase,
        _ => throw new ArgumentOutOfRangeException(nameof(message)),
    };

    private static int UsableLength(FieldDescriptor field) =>
        field.GetOptions().GetExtension(NanopbExtensions.Nanopb).MaxSize - 1;

    // Mirrors the generated nanopb _size constants (events.pb.h) that bound each
    // WebSocket frame. Changing a limit means regenerating and updating the wiki.
    private static int MaxFrameBytes(bool fromDevice) => fromDevice ? 328 : 1326;

    private static IReadOnlyDictionary<string, Frame> LoadFrames()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "testdata", "frames.txt");
        var frames = new Dictionary<string, Frame>(StringComparer.Ordinal);
        foreach (var line in File.ReadLines(path))
        {
            if (line.Length == 0 || line.StartsWith('#'))
            {
                continue;
            }

            var parts = line.Split(' ');
            Assert.True(parts.Length == 4, $"Malformed frame line: {line}");
            Assert.True(parts[0] is "d2s" or "b2d", $"Unknown direction in: {line}");
            var frame = new Frame(parts[0] == "d2s", parts[1], parts[2], Convert.FromHexString(parts[3]));
            Assert.True(frames.TryAdd($"{parts[0]} {parts[1]}", frame), $"Duplicate frame: {line}");
        }

        return frames;
    }

    private sealed record Frame(bool FromDevice, string Name, string Kind, byte[] Bytes);
}
