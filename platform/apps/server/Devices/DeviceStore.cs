using Mosaic.Server.Auth;
using Mosaic.Server.Data;

namespace Mosaic.Server.Devices;

public enum CredentialState
{
    /// <summary>No device has this credential, so it may be given a pairing code.</summary>
    Unknown,

    /// <summary>An admin claimed this credential, so it is authenticated.</summary>
    Paired,

    /// <summary>The admin removed this device. The hash is kept so the old secret can never return.</summary>
    Revoked,
}

/// <summary>What is stored for a credential. <see cref="DeviceId"/> and <see cref="PairedAt"/> are set unless it is unknown.</summary>
public sealed record DeviceCredential(CredentialState State, long? DeviceId, DateTimeOffset? PairedAt);

/// <summary>A paired device: its ID and when it was first paired.</summary>
public sealed record PairedDevice(long Id, DateTimeOffset PairedAt);

/// <summary>A paired device as stored, with the secret hash that finds its connection. The hash never leaves the server.</summary>
public sealed record StoredDevice(long Id, DateTimeOffset PairedAt, byte[] SecretHash);

/// <summary>
/// Paired devices in SQLite, keyed by the SHA-256 of their secret (see <see cref="DeviceSecret"/>).
/// No plain secret is ever stored. Revoked rows stay so a revoked secret is recognized as such.
/// </summary>
public sealed class DeviceStore(MosaicDatabase database, TimeProvider time)
{
    public DeviceCredential Find(byte[] secretHash)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand("SELECT id, paired_at, revoked_at FROM devices WHERE secret_hash = $secretHash");
        command.Parameters.AddWithValue("$secretHash", secretHash);
        using var reader = command.ExecuteReader();
        if (!reader.Read())
        {
            return new DeviceCredential(CredentialState.Unknown, null, null);
        }

        return new DeviceCredential(
            reader.IsDBNull(2) ? CredentialState.Paired : CredentialState.Revoked,
            reader.GetInt64(0),
            Timestamps.Parse(reader.GetString(1)));
    }

    /// <summary>
    /// Records a claimed credential as paired and returns the device. A credential that is
    /// already paired keeps its ID and original pairing time, so a retried claim resolves to the
    /// same device. Returns null for a revoked credential, which can never be paired again.
    /// </summary>
    public PairedDevice? Pair(byte[] secretHash)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand("""
            INSERT INTO devices (secret_hash, paired_at) VALUES ($secretHash, $pairedAt)
            ON CONFLICT (secret_hash) DO UPDATE SET secret_hash = excluded.secret_hash WHERE revoked_at IS NULL
            RETURNING id, paired_at
            """);
        command.Parameters.AddWithValue("$secretHash", secretHash);
        command.Parameters.AddWithValue("$pairedAt", Timestamps.Format(time.GetUtcNow()));
        using var reader = command.ExecuteReader();
        return reader.Read() ? new PairedDevice(reader.GetInt64(0), Timestamps.Parse(reader.GetString(1))) : null;
    }

    /// <summary>Every paired device, oldest pairing first. Revoked devices are left out.</summary>
    public IReadOnlyList<StoredDevice> ListPaired()
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand(
            "SELECT id, paired_at, secret_hash FROM devices WHERE revoked_at IS NULL ORDER BY id");
        using var reader = command.ExecuteReader();
        var devices = new List<StoredDevice>();
        while (reader.Read())
        {
            devices.Add(new StoredDevice(reader.GetInt64(0), Timestamps.Parse(reader.GetString(1)), (byte[])reader[2]));
        }

        return devices;
    }

    /// <summary>
    /// Revokes a paired device and returns its secret hash, so its connection can be told. Returns
    /// null when it is missing or already revoked, so of two removals at once only one succeeds.
    /// The row and hash are kept, so the old secret is recognized and rejected from then on.
    /// </summary>
    public byte[]? Revoke(long deviceId)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand(
            "UPDATE devices SET revoked_at = $revokedAt WHERE id = $id AND revoked_at IS NULL RETURNING secret_hash");
        command.Parameters.AddWithValue("$revokedAt", Timestamps.Format(time.GetUtcNow()));
        command.Parameters.AddWithValue("$id", deviceId);
        return command.ExecuteScalar() as byte[];
    }
}
