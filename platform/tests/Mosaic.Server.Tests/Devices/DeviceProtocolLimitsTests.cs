using System.Text.RegularExpressions;
using Google.Protobuf.Reflection;
using Mosaic.Device.V1;
using Mosaic.Server.Devices;

namespace Mosaic.Server.Tests.Devices;

/// <summary>
/// The server's own field and frame limits must fit what nanopb on the device can hold, since the
/// C# bindings enforce none of them.
/// </summary>
public sealed partial class DeviceProtocolLimitsTests
{
    [Fact]
    public void FieldRulesFitTheNanopbLimits()
    {
        var secret = Nanopb(DeviceConnect.Descriptor, "auth_secret");
        Assert.Equal(DeviceSecret.SecretSize, secret.MaxSize);
        Assert.True(secret.FixedLength);

        // A nanopb string max_size includes the terminating NUL.
        Assert.Equal(PairingCodes.CodeLength, Nanopb(PairingPending.Descriptor, "code").MaxSize - 1);
    }

    [Fact]
    public void FrameLimitsAreTheFixedProtocolValues()
    {
        // Part of protocol version 1: changing them needs a version bump, so a change here is deliberate.
        Assert.Equal(512, DeviceEndpoint.MaxDeviceFrameBytes);
        Assert.Equal(4096, DeviceEndpoint.MaxServerFrameBytes);
    }

    [Fact]
    public void EveryGeneratedMessageFitsItsFrameLimit()
    {
        // A schema change that would outgrow a limit needs a protocol version bump instead.
        var header = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "testdata", "events.pb.h"));

        Assert.True(Size(header, "DeviceToBackendEnvelope") <= DeviceEndpoint.MaxDeviceFrameBytes);
        Assert.True(Size(header, "BackendToDeviceEnvelope") <= DeviceEndpoint.MaxServerFrameBytes);
    }

    private static NanoPBOptions Nanopb(MessageDescriptor message, string field) =>
        message.FindFieldByName(field).GetOptions().GetExtension(NanopbExtensions.Nanopb);

    private static int Size(string header, string message)
    {
        var match = SizeDefine().Matches(header).Single(match => match.Groups[1].Value == message);
        return int.Parse(match.Groups[2].Value, System.Globalization.CultureInfo.InvariantCulture);
    }

    [GeneratedRegex(@"#define mosaic_device_v1_(\w+)_size\s+(\d+)")]
    private static partial Regex SizeDefine();
}
