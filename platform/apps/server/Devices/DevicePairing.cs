namespace Mosaic.Server.Devices;

/// <summary>
/// What a connecting credential is now: paired or revoked as stored, or unknown with the code it
/// should show. An unknown credential with no code means every pairing slot is taken.
/// </summary>
public sealed record Admission(DeviceCredential Credential, PendingCode? Code);

public enum ClaimOutcome
{
    /// <summary>The code paired its device, now or in an earlier claim.</summary>
    Paired,

    /// <summary>No live or remembered code matches.</summary>
    Unknown,

    /// <summary>The code paired a device that has since been removed.</summary>
    Used,

    /// <summary>The code ran out before it was claimed.</summary>
    Expired,
}

/// <summary>The result of a claim. <see cref="Device"/> and <see cref="Connected"/> are set when it paired.</summary>
public sealed record ClaimResult(ClaimOutcome Outcome, PairedDevice? Device = null, bool Connected = false);

/// <summary>
/// Pairs devices: decides whether a connecting credential gets a code and claims codes for the
/// admin (Admin API Contract, section 5). Both read the stored credential and the codes under one
/// lock, so a claim and a connecting device never act on each other's stale state. Without it, a
/// device could read "unknown" just before a claim commits and be handed a new code once paired.
/// </summary>
/// <remarks>
/// The lock covers one short SQLite statement at most and never socket I/O. A claim commits
/// ownership before its code is marked used, so a failed write leaves the code claimable.
/// </remarks>
public sealed class DevicePairing(DeviceStore devices, PairingCodes codes, DeviceConnections connections)
{
    private readonly Lock _lock = new();

    /// <summary>
    /// For a device that connected or whose code ran out. Register the connection first, so a
    /// claim that commits after this reads the store finds the connection to notify.
    /// </summary>
    public Admission Admit(byte[] secretHash)
    {
        lock (_lock)
        {
            var credential = devices.Find(secretHash);
            return credential.State == CredentialState.Unknown
                ? new Admission(credential, codes.Issue(secretHash))
                : new Admission(credential, null);
        }
    }

    /// <summary>
    /// Claims a code in the stored form from <see cref="PairingCodes.TryNormalize"/>. A live code
    /// pairs its credential and becomes used; a used one returns the same device again, with
    /// <see cref="ClaimResult.Connected"/> as of now. A connected device is told it is paired.
    /// </summary>
    public ClaimResult Claim(string code)
    {
        PairedDevice? device;
        byte[] secretHash;
        lock (_lock)
        {
            var lookup = codes.Find(code);
            switch (lookup.State)
            {
                case CodeState.Live:
                    secretHash = lookup.SecretHash!;
                    device = devices.Pair(secretHash);
                    codes.MarkUsed(code);
                    break;
                case CodeState.Used:
                    secretHash = lookup.SecretHash!;
                    var credential = devices.Find(secretHash);
                    device = credential.State == CredentialState.Paired
                        ? new PairedDevice(credential.DeviceId!.Value, credential.PairedAt!.Value)
                        : null;
                    break;
                case CodeState.Expired:
                    return new ClaimResult(ClaimOutcome.Expired);
                default:
                    return new ClaimResult(ClaimOutcome.Unknown);
            }
        }

        // Only a credential removed after it paired gets here without a device.
        if (device is null)
        {
            return new ClaimResult(ClaimOutcome.Used);
        }

        // A retry signals too. A device already told it is paired ignores the signal.
        return new ClaimResult(ClaimOutcome.Paired, device, connections.NotifyPaired(secretHash));
    }
}
