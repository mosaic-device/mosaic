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
        Assert.True(ConnectCheck.MaxRequestIdLength <= UsableLength(DeviceToBackendEnvelope.Descriptor, "request_id"));
        Assert.True(ConnectCheck.MacAddressLength <= UsableLength(DeviceConnect.Descriptor, "mac_address"));
        Assert.True(DeviceSecret.TextLength <= UsableLength(DeviceConnect.Descriptor, "auth_secret"));
        Assert.Equal(PairingCodes.CodeLength, UsableLength(PairingPending.Descriptor, "code"));
    }

    [Fact]
    public void FrameLimitsMatchTheGeneratedNanopbSizes()
    {
        var header = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "testdata", "events.pb.h"));

        Assert.Equal(DeviceEndpoint.MaxDeviceFrameBytes, Size(header, "DeviceToBackendEnvelope"));
        Assert.Equal(DeviceEndpoint.MaxServerFrameBytes, Size(header, "BackendToDeviceEnvelope"));
    }

    private static int UsableLength(MessageDescriptor message, string field) =>
        message.FindFieldByName(field).GetOptions().GetExtension(NanopbExtensions.Nanopb).MaxSize - 1;

    private static int Size(string header, string message)
    {
        var match = SizeDefine().Matches(header).Single(match => match.Groups[1].Value == message);
        return int.Parse(match.Groups[2].Value, System.Globalization.CultureInfo.InvariantCulture);
    }

    [GeneratedRegex(@"#define mosaic_device_v1_(\w+)_size\s+(\d+)")]
    private static partial Regex SizeDefine();
}
