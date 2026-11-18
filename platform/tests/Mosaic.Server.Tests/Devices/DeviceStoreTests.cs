using System.Security.Cryptography;
using Mosaic.Server.Data;
using Mosaic.Server.Devices;

namespace Mosaic.Server.Tests.Devices;

/// <summary>The paired credentials <see cref="DeviceStore.IsPaired"/> answers from memory.</summary>
public sealed class DeviceStoreTests : IDisposable
{
    private readonly string _directory = Path.Combine(Path.GetTempPath(), "mosaic-tests", Guid.NewGuid().ToString("N"));

    private readonly MosaicDatabase _database;

    public DeviceStoreTests()
    {
        _database = new MosaicDatabase(Path.Combine(_directory, "mosaic.db"));
        _database.Migrate();
    }

    public void Dispose()
    {
        if (Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }

    [Fact]
    public void ARestartedServerKnowsWhichCredentialsArePaired()
    {
        var before = new DeviceStore(_database, TimeProvider.System);
        var paired = Hash();
        var revoked = Hash();
        before.Pair(paired);
        before.Revoke(before.Pair(revoked)!.Id);

        var after = new DeviceStore(_database, TimeProvider.System);
        after.LoadPaired();

        Assert.True(after.IsPaired(paired));
        Assert.False(after.IsPaired(revoked));
        Assert.False(after.IsPaired(Hash()));
    }

    [Fact]
    public void PairingAndRemovalUpdateTheCredentialsInMemory()
    {
        var devices = new DeviceStore(_database, TimeProvider.System);
        devices.LoadPaired();
        var secretHash = Hash();
        Assert.False(devices.IsPaired(secretHash));

        var id = devices.Pair(secretHash)!.Id;
        Assert.True(devices.IsPaired(secretHash));

        Assert.NotNull(devices.Revoke(id));
        Assert.False(devices.IsPaired(secretHash));

        // A revoked credential can't be paired again, so it stays out.
        Assert.Null(devices.Pair(secretHash));
        Assert.False(devices.IsPaired(secretHash));
        Assert.Null(devices.Revoke(id));
        Assert.False(devices.IsPaired(secretHash));
    }

    [Fact]
    public void ACredentialPairedBeforeTheFirstCheckIsFound()
    {
        var devices = new DeviceStore(_database, TimeProvider.System);
        var secretHash = Hash();

        // Paired before anything loaded the set: the first check reads it from the database.
        devices.Pair(secretHash);

        Assert.True(devices.IsPaired(secretHash));
    }

    private static byte[] Hash() => SHA256.HashData(RandomNumberGenerator.GetBytes(32));
}
