using System.Buffers.Text;
using System.Security.Cryptography;

namespace Mosaic.Server.Devices;

/// <summary>
/// The device's credential (Device Connection Protocol, section 8): 32 random bytes the device
/// creates, sent as unpadded Base64Url. The server keeps only the SHA-256 of the decoded bytes,
/// never the text, and never logs either.
/// </summary>
public static class DeviceSecret
{
    public const int SecretSize = 32;

    // SecretSize bytes in unpadded Base64Url.
    public const int TextLength = 43;

    /// <summary>Hashes <paramref name="text"/> when it is exactly a 32-byte secret in unpadded Base64Url.</summary>
    public static bool TryHash(string text, out byte[] hash)
    {
        hash = [];
        if (text.Length != TextLength || !Base64Url.IsValid(text, out var decodedLength) || decodedLength != SecretSize)
        {
            return false;
        }

        Span<byte> secret = stackalloc byte[SecretSize];
        try
        {
            if (Base64Url.DecodeFromChars(text, secret) != SecretSize)
            {
                return false;
            }

            hash = SHA256.HashData(secret);
            return true;
        }
        finally
        {
            CryptographicOperations.ZeroMemory(secret);
        }
    }
}
