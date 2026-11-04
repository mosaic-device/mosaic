using Mosaic.Device.V1;

namespace Mosaic.Server.Devices;

/// <summary>A <c>connect</c> frame that passed every field rule, ready for the credential lookup.</summary>
public sealed record ValidConnect(string RequestId, byte[] SecretHash, string MacAddress);

/// <summary>
/// The result of checking a device's first frame: either <see cref="Connect"/> or a
/// <see cref="Rejection"/>. <see cref="RequestId"/> is echoed in the reply when it was well formed.
/// </summary>
public sealed record ConnectCheckResult(ValidConnect? Connect, ConnectionRejectReason? Rejection, string RequestId);

/// <summary>
/// Receiver-enforced rules for the device's first frame (Device Connection Protocol, sections 3–5).
/// Proto3 has no required fields and the C# bindings enforce no nanopb limits, so every rule
/// here is checked explicitly. The version is checked first, then the fields; the credential
/// lookup comes after this.
/// </summary>
public static class ConnectCheck
{
    public static readonly IReadOnlySet<uint> SupportedVersions = new HashSet<uint> { 1 };

    public const int MaxRequestIdLength = 63;
    public const int MacAddressLength = 17;

    public static ConnectCheckResult Check(DeviceToBackendEnvelope frame)
    {
        // Only echo a request_id that is itself valid, so a reply never carries untrusted text.
        var requestId = IsValidRequestId(frame.RequestId) ? frame.RequestId : "";

        // Before the handshake, anything but connect is a protocol error, including an empty
        // envelope or an unknown payload (section 3, item 4).
        if (frame.PayloadCase != DeviceToBackendEnvelope.PayloadOneofCase.Connect)
        {
            return Reject(ConnectionRejectReason.MalformedRequest, requestId);
        }

        var connect = frame.Connect;
        if (!SupportedVersions.Contains(connect.ProtocolVersion))
        {
            return Reject(ConnectionRejectReason.UnsupportedVersion, requestId);
        }

        if (requestId.Length == 0
            || !IsValidMacAddress(connect.MacAddress)
            || !DeviceSecret.TryHash(connect.AuthSecret, out var secretHash))
        {
            return Reject(ConnectionRejectReason.MalformedRequest, requestId);
        }

        return new ConnectCheckResult(new ValidConnect(requestId, secretHash, connect.MacAddress), null, requestId);
    }

    /// <summary>1–63 printable ASCII characters.</summary>
    public static bool IsValidRequestId(string value) =>
        value.Length is > 0 and <= MaxRequestIdLength && value.All(character => character is >= ' ' and <= '~');

    /// <summary>Six uppercase hex pairs separated by colons, such as <c>AA:BB:CC:DD:EE:FF</c>.</summary>
    public static bool IsValidMacAddress(string value)
    {
        if (value.Length != MacAddressLength)
        {
            return false;
        }

        for (var i = 0; i < value.Length; i++)
        {
            var valid = i % 3 == 2 ? value[i] == ':' : value[i] is >= '0' and <= '9' or >= 'A' and <= 'F';
            if (!valid)
            {
                return false;
            }
        }

        return true;
    }

    private static ConnectCheckResult Reject(ConnectionRejectReason reason, string requestId) => new(null, reason, requestId);
}
