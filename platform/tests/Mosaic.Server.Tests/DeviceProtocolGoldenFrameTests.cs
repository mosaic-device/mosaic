using Google.Protobuf;
using Google.Protobuf.Reflection;
using Mosaic.Device.V1;

namespace Mosaic.Server.Tests;

// Checks the C# bindings against platform/packages/protocols/testdata/frames.txt.
// The firmware host test checks the same file with nanopb, so both sides agree on
// every byte. See the wiki's Device Connection Protocol.
public sealed class DeviceProtocolGoldenFrameTests
{
    private static readonly ByteString AuthSecret = ByteString.CopyFrom([.. Enumerable.Range(0, 32).Select(value => (byte)value)]);

    private static readonly IReadOnlyDictionary<string, IMessage> ValidDeviceFrames = new Dictionary<string, IMessage>
    {
        ["connect"] = new DeviceToBackendEnvelope
        {
            Connect = new DeviceConnect { ProtocolVersion = 1, AuthSecret = AuthSecret },
        },
    };

    private static readonly IReadOnlyDictionary<string, IMessage> ValidServerFrames = new Dictionary<string, IMessage>
    {
        ["paired"] = new BackendToDeviceEnvelope { Paired = new DevicePaired() },
        ["pairing_pending"] = new BackendToDeviceEnvelope { PairingPending = Pending() },
        ["rejected_malformed_request"] = Rejected(ConnectionRejectReason.MalformedRequest),
        ["rejected_unsupported_version"] = Rejected(ConnectionRejectReason.UnsupportedVersion),
        ["rejected_credential_revoked"] = Rejected(ConnectionRejectReason.CredentialRevoked),
        ["rejected_server_busy"] = Rejected(ConnectionRejectReason.ServerBusy),
    };

    private static readonly string[] IgnoredFrames = ["d2s unknown_payload", "b2d unknown_payload"];

    // Each frame decodes in C#, but the field's length breaks its nanopb limit.
    private static readonly IReadOnlyDictionary<string, Func<IMessage, bool>> BadLengthFrames =
        new Dictionary<string, Func<IMessage, bool>>
        {
            ["d2s connect_secret_long"] = message => SecretLength(message) > AuthSecretLength,
            ["d2s connect_secret_short"] = message => SecretLength(message) < AuthSecretLength,
            ["b2d pairing_pending_code_long"] = message =>
                ((BackendToDeviceEnvelope)message).PairingPending.Code.Length
                    > UsableStringLength(PairingPending.Descriptor.FindFieldByName("code")),
        };

    private static int AuthSecretLength => MaxSize(DeviceConnect.Descriptor.FindFieldByName("auth_secret"));

    private static readonly string[] TruncatedFrames = ["d2s connect_truncated", "b2d pairing_pending_truncated"];

    public static TheoryData<string> FrameKeys => [.. Frames.Value.Keys];

    private static readonly Lazy<IReadOnlyDictionary<string, Frame>> Frames = new(LoadFrames);

    [Fact]
    public void FixtureAndExpectationsCoverTheSameFrames()
    {
        var expected = ValidDeviceFrames.Keys.Select(name => $"d2s {name}")
            .Concat(ValidServerFrames.Keys.Select(name => $"b2d {name}"))
            .Concat(IgnoredFrames)
            .Concat(BadLengthFrames.Keys)
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
                Assert.Contains(key, IgnoredFrames);
                Assert.Equal(0, PayloadCase(Parse(frame)));
                break;

            case "bad_length":
                // The C# bindings enforce no nanopb limits, so this decodes. The server
                // must reject it by comparing the field with its declared limit.
                Assert.True(BadLengthFrames[key](Parse(frame)));
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
        // instead of copying them.
        var secret = DeviceConnect.Descriptor.FindFieldByName("auth_secret");
        Assert.Equal(32, MaxSize(secret));
        Assert.True(secret.GetOptions().GetExtension(NanopbExtensions.Nanopb).FixedLength);
        Assert.Equal(8, UsableStringLength(PairingPending.Descriptor.FindFieldByName("code")));
    }

    private static PairingPending Pending() => new() { Code = "K7QF2M9X", ExpiresInSeconds = 600 };

    private static BackendToDeviceEnvelope Rejected(ConnectionRejectReason reason) =>
        new() { Rejected = new ConnectionRejected { Reason = reason } };

    private static IMessage Parse(Frame frame) => frame.FromDevice
        ? DeviceToBackendEnvelope.Parser.ParseFrom(frame.Bytes)
        : BackendToDeviceEnvelope.Parser.ParseFrom(frame.Bytes);

    private static int PayloadCase(IMessage message) => message switch
    {
        DeviceToBackendEnvelope device => (int)device.PayloadCase,
        BackendToDeviceEnvelope server => (int)server.PayloadCase,
        _ => throw new ArgumentOutOfRangeException(nameof(message)),
    };

    private static int SecretLength(IMessage message) => ((DeviceToBackendEnvelope)message).Connect.AuthSecret.Length;

    private static int MaxSize(FieldDescriptor field) => field.GetOptions().GetExtension(NanopbExtensions.Nanopb).MaxSize;

    // A nanopb string max_size includes the terminating NUL.
    private static int UsableStringLength(FieldDescriptor field) => MaxSize(field) - 1;

    // Mirrors the generated nanopb _size constants (events.pb.h) that bound each
    // WebSocket frame. Changing a limit means regenerating and updating the wiki.
    private static int MaxFrameBytes(bool fromDevice) => fromDevice ? 42 : 18;

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
