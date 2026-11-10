using System.Collections.Frozen;
using Mosaic.Device.V1;

namespace Mosaic.Server.Devices;

/// <summary>A <c>connect</c> frame that passed every field rule, ready for the credential lookup.</summary>
public sealed record ValidConnect(byte[] SecretHash);

/// <summary>The result of checking a device's first frame: either <see cref="Connect"/> or a <see cref="Rejection"/>.</summary>
public sealed record ConnectCheckResult(ValidConnect? Connect, ConnectionRejectReason? Rejection);

/// <summary>
/// Receiver-enforced rules for the device's first frame (Device Connection Protocol, sections 3–5).
/// Proto3 has no required fields and the C# bindings enforce no nanopb limits, so every rule
/// here is checked explicitly. The version is checked first, then the fields; the credential
/// lookup comes after this.
/// </summary>
public static class ConnectCheck
{
    /// <summary>Protocol versions the server accepts. Frozen, so nothing can change them at run time.</summary>
    public static readonly FrozenSet<uint> SupportedVersions = FrozenSet.Create<uint>(1);

    public static ConnectCheckResult Check(DeviceToBackendEnvelope frame)
    {
        // Before the handshake, anything but connect is a protocol error, including an empty
        // envelope or an unknown payload (section 3, item 4).
        if (frame.PayloadCase != DeviceToBackendEnvelope.PayloadOneofCase.Connect)
        {
            return Reject(ConnectionRejectReason.MalformedRequest);
        }

        var connect = frame.Connect;
        if (!SupportedVersions.Contains(connect.ProtocolVersion))
        {
            return Reject(ConnectionRejectReason.UnsupportedVersion);
        }

        if (!DeviceSecret.TryHash(connect.AuthSecret.Span, out var secretHash))
        {
            return Reject(ConnectionRejectReason.MalformedRequest);
        }

        return new ConnectCheckResult(new ValidConnect(secretHash), null);
    }

    private static ConnectCheckResult Reject(ConnectionRejectReason reason) => new(null, reason);
}
