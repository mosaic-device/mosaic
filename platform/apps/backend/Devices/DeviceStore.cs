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

public sealed record DeviceCredential(CredentialState State, long? DeviceId);

/// <summary>
/// Paired devices in SQLite, keyed by the SHA-256 of their secret (see <see cref="DeviceSecret"/>).
/// No plain secret is ever stored. Revoked rows stay so a revoked secret is recognized as such.
/// </summary>
public sealed class DeviceStore(MosaicDatabase database, TimeProvider time)
{
    public DeviceCredential Find(byte[] secretHash)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand("SELECT id, revoked_at FROM devices WHERE secret_hash = $secretHash");
        command.Parameters.AddWithValue("$secretHash", secretHash);
        using var reader = command.ExecuteReader();
        if (!reader.Read())
        {
            return new DeviceCredential(CredentialState.Unknown, null);
        }

        var id = reader.GetInt64(0);
        return new DeviceCredential(reader.IsDBNull(1) ? CredentialState.Paired : CredentialState.Revoked, id);
    }

    /// <summary>
    /// Records a claimed credential as paired and returns its device ID. A credential that is
    /// already paired keeps its ID, so a retried claim resolves to the same device. Returns
    /// null for a revoked credential, which can never be paired again.
    /// </summary>
    public long? Pair(byte[] secretHash, string macAddress)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand("""
            INSERT INTO devices (secret_hash, mac_address, paired_at) VALUES ($secretHash, $macAddress, $pairedAt)
            ON CONFLICT (secret_hash) DO UPDATE SET secret_hash = excluded.secret_hash WHERE revoked_at IS NULL
            RETURNING id
            """);
        command.Parameters.AddWithValue("$secretHash", secretHash);
        command.Parameters.AddWithValue("$macAddress", macAddress);
        command.Parameters.AddWithValue("$pairedAt", Timestamps.Format(time.GetUtcNow()));
        return command.ExecuteScalar() is long id ? id : null;
    }

    /// <summary>Revokes a paired device. Returns false when it is missing or already revoked.</summary>
    public bool Revoke(long deviceId)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand(
            "UPDATE devices SET revoked_at = $revokedAt WHERE id = $id AND revoked_at IS NULL");
        command.Parameters.AddWithValue("$revokedAt", Timestamps.Format(time.GetUtcNow()));
        command.Parameters.AddWithValue("$id", deviceId);
        return command.ExecuteNonQuery() > 0;
    }
}
