using System.Security.Cryptography;

namespace Mosaic.Server.Devices;

/// <summary>
/// The device's credential (Device Connection Protocol, section 8): 32 random bytes the device
/// creates and sends as-is. The server keeps only their SHA-256 and never logs either.
/// </summary>
public static class DeviceSecret
{
    public const int SecretSize = 32;

    /// <summary>Hashes <paramref name="secret"/> when it is exactly <see cref="SecretSize"/> bytes.</summary>
    public static bool TryHash(ReadOnlySpan<byte> secret, out byte[] hash)
    {
        if (secret.Length != SecretSize)
        {
            hash = [];
            return false;
        }

        hash = SHA256.HashData(secret);
        return true;
    }
}
