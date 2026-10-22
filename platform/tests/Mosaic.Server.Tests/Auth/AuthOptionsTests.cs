using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests.Auth;

public sealed class AuthOptionsTests
{
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    public void PublicOriginIsRequired(string? value)
    {
        var error = Assert.Throws<InvalidOperationException>(() =>
            AuthOptions.FromConfiguration(TestConfiguration.From((AuthOptions.PublicOriginKey, value))));

        Assert.Contains(AuthOptions.PublicOriginKey, error.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("https://mosaic.example", "https://mosaic.example")]
    [InlineData("https://Mosaic.Example/", "https://mosaic.example")]
    [InlineData("https://mosaic.example:443", "https://mosaic.example")]
    [InlineData("http://localhost:5000", "http://localhost:5000")]
    [InlineData(" http://localhost:80/ ", "http://localhost")]
    public void PublicOriginIsNormalizedLikeABrowserOrigin(string configured, string expected) =>
        Assert.Equal(expected, TestConfiguration.Options(configured).PublicOrigin);

    [Theory]
    [InlineData("https://mosaic.example/admin")]
    [InlineData("https://mosaic.example/?x=1")]
    [InlineData("ftp://mosaic.example")]
    [InlineData("mosaic.example")]
    [InlineData("not a url")]
    public void PublicOriginMustBeAnHttpOrigin(string configured) =>
        Assert.Throws<InvalidOperationException>(() => TestConfiguration.Options(configured));

    [Fact]
    public void AllowedOriginsAreThePublicOriginPlusAdditionalOnes()
    {
        var options = TestConfiguration.Options("https://mosaic.test", true, "http://localhost:5173/", "");

        Assert.Equal(["https://mosaic.test", "http://localhost:5173"], options.AllowedOrigins);
    }

    [Theory]
    [InlineData("https://mosaic.test", true)]
    [InlineData("HTTPS://MOSAIC.TEST", true)]
    [InlineData("http://localhost:5173", true)]
    [InlineData("http://mosaic.test", false)]
    [InlineData("https://mosaic.test:8443", false)]
    [InlineData("https://evil.example", false)]
    [InlineData("null", false)]
    [InlineData("", false)]
    [InlineData(null, false)]
    public void IsAllowedOriginMatchesExactly(string? origin, bool allowed) =>
        Assert.Equal(allowed, TestConfiguration.Options("https://mosaic.test", true, "http://localhost:5173").IsAllowedOrigin(origin));

    [Theory]
    [InlineData(null, true)]
    [InlineData("true", true)]
    [InlineData("False", false)]
    public void SecureCookieDefaultsToOn(string? configured, bool expected)
    {
        var options = AuthOptions.FromConfiguration(TestConfiguration.From(
            (AuthOptions.PublicOriginKey, "https://mosaic.test"),
            (AuthOptions.SecureCookieKey, configured)));

        Assert.Equal(expected, options.SecureCookie);
    }

    [Fact]
    public void InvalidSecureCookieNamesTheSetting()
    {
        var error = Assert.Throws<InvalidOperationException>(() => AuthOptions.FromConfiguration(TestConfiguration.From(
            (AuthOptions.PublicOriginKey, "https://mosaic.test"),
            (AuthOptions.SecureCookieKey, "maybe"))));

        Assert.Contains(AuthOptions.SecureCookieKey, error.Message, StringComparison.Ordinal);
    }
}
