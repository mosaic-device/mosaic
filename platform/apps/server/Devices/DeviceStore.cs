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
/// <remarks>
/// The hashes of paired devices are also kept in memory for <see cref="IsPaired"/>, which runs
/// before a device's upgrade is admitted, so anyone can call it. It is loaded once from the
/// database and kept in step by <see cref="Pair"/> and <see cref="Revoke"/>, which update it
/// under the same lock as their write. This process is the database's only writer.
/// </remarks>
public sealed class DeviceStore(MosaicDatabase database, TimeProvider time)
{
    private readonly Lock _pairedLock = new();
    private HashSet<string>? _paired;

    /// <summary>
    /// Whether this credential is paired, from memory, with no database work after the first
    /// call. For admission only: <see cref="Find"/> stays the authority on a connection.
    /// </summary>
    public bool IsPaired(byte[] secretHash)
    {
        lock (_pairedLock)
        {
            return Paired().Contains(Convert.ToHexString(secretHash));
        }
    }

    /// <summary>Loads the paired hashes now, at startup after migration, so no upgrade waits on it.</summary>
    public void LoadPaired()
    {
        lock (_pairedLock)
        {
            Paired();
        }
    }

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
        lock (_pairedLock)
        {
            var device = PairRow(secretHash);
            if (device is not null)
            {
                _paired?.Add(Convert.ToHexString(secretHash));
            }

            return device;
        }
    }

    private PairedDevice? PairRow(byte[] secretHash)
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

    /// <summary>The secret hash of the paired device with this ID, or null when there is none or it was removed.</summary>
    public byte[]? FindPaired(long deviceId)
    {
        using var connection = database.OpenConnection();
        using var command = connection.CreateCommand(
            "SELECT secret_hash FROM devices WHERE id = $id AND revoked_at IS NULL");
        command.Parameters.AddWithValue("$id", deviceId);
        return command.ExecuteScalar() as byte[];
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
        lock (_pairedLock)
        {
            using var connection = database.OpenConnection();
            using var command = connection.CreateCommand(
                "UPDATE devices SET revoked_at = $revokedAt WHERE id = $id AND revoked_at IS NULL RETURNING secret_hash");
            command.Parameters.AddWithValue("$revokedAt", Timestamps.Format(time.GetUtcNow()));
            command.Parameters.AddWithValue("$id", deviceId);
            var secretHash = command.ExecuteScalar() as byte[];
            if (secretHash is not null)
            {
                _paired?.Remove(Convert.ToHexString(secretHash));
            }

            return secretHash;
        }
    }

    // Called under _pairedLock. Loaded on first use rather than at construction, since the
    // database may not be migrated yet when the store is created.
    private HashSet<string> Paired()
    {
        if (_paired is null)
        {
            var paired = new HashSet<string>(StringComparer.Ordinal);
            foreach (var device in ListPaired())
            {
                paired.Add(Convert.ToHexString(device.SecretHash));
            }

            _paired = paired;
        }

        return _paired;
    }
}
