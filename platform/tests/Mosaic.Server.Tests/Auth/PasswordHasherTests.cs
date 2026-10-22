using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests.Auth;

public sealed class PasswordHasherTests
{
    // The minimum work factor keeps these tests fast; the format and checks are the same.
    private readonly PasswordHasher _hasher = new(PasswordHasher.MinIterations);

    [Fact]
    public void HashVerifiesTheSamePasswordOnly()
    {
        var stored = _hasher.Hash("mosaic-desk-2026");

        Assert.True(_hasher.Verify("mosaic-desk-2026", stored));
        Assert.False(_hasher.Verify("mosaic-desk-2027", stored));
        Assert.False(_hasher.Verify("", stored));
    }

    [Fact]
    public void HashIsSelfDescribingAndSalted()
    {
        var first = _hasher.Hash("same-password");
        var second = _hasher.Hash("same-password");

        Assert.NotEqual(first, second);
        var parts = first.Split('$');
        Assert.Equal("pbkdf2-sha256", parts[0]);
        Assert.Equal("10000", parts[1]);
        Assert.Equal(PasswordHasher.SaltSize, Convert.FromBase64String(parts[2]).Length);
        Assert.Equal(PasswordHasher.HashSize, Convert.FromBase64String(parts[3]).Length);
    }

    [Fact]
    public void VerifyUsesTheStoredWorkFactor()
    {
        var stored = new PasswordHasher(PasswordHasher.MinIterations + 1).Hash("password-1234");

        Assert.True(new PasswordHasher().Verify("password-1234", stored));
    }

    [Fact]
    public void DefaultWorkFactorFollowsOwasp() => Assert.Equal(600_000, new PasswordHasher().Iterations);

    [Theory]
    [InlineData("pbkdf2-sha256$600000$AAAAAAAAAAAAAAAAAAAAAA==$")] // empty digest
    [InlineData("pbkdf2-sha256$600000$$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")] // empty salt
    [InlineData("pbkdf2-sha256$600000$AAAAAAAAAAAAAAAAAAAAAA==$AAAA")] // short digest
    [InlineData("pbkdf2-sha256$600000$AAAA$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")] // short salt
    [InlineData("pbkdf2-sha256$600000$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==")] // long digest
    [InlineData("pbkdf2-sha256$600000$not base64!$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("pbkdf2-sha256$0$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("pbkdf2-sha256$9999$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("pbkdf2-sha256$10000001$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("pbkdf2-sha256$-600000$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("pbkdf2-sha256$+600000$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("pbkdf2-sha512$600000$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")]
    [InlineData("pbkdf2-sha256$600000$AAAAAAAAAAAAAAAAAAAAAA==")]
    [InlineData("pbkdf2-sha256$600000$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=$extra")]
    [InlineData("plain-text-password")]
    [InlineData("")]
    public void MalformedStoredHashesNeverVerify(string stored) => Assert.False(_hasher.Verify("any-password", stored));

    [Fact]
    public void DummyVerificationAlwaysFails() => Assert.False(_hasher.VerifyAgainstDummy("any-password"));

    [Theory]
    [InlineData(PasswordHasher.MinIterations - 1)]
    [InlineData(PasswordHasher.MaxIterations + 1)]
    public void WorkFactorMustBeWithinBounds(int iterations) =>
        Assert.Throws<ArgumentOutOfRangeException>(() => new PasswordHasher(iterations));
}
