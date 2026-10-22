using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests.Auth;

public sealed class AuthRateLimitOptionsTests
{
    [Fact]
    public void DefaultsAllowAFewAttemptsAndBoundHashing()
    {
        var limits = AuthRateLimitOptions.FromConfiguration(TestConfiguration.From());

        Assert.Equal(10, limits.AttemptsPerMinute);
        Assert.Equal(Math.Max(1, Environment.ProcessorCount / 2), limits.ConcurrentPasswordChecks);
        Assert.Equal(16, limits.QueuedPasswordChecks);
        Assert.Equal(TimeSpan.FromSeconds(10), limits.BodyReadTimeout);
    }

    [Fact]
    public void LimitsComeFromConfiguration()
    {
        var limits = AuthRateLimitOptions.FromConfiguration(TestConfiguration.From(
            (AuthRateLimitOptions.AttemptsPerMinuteKey, "5"),
            (AuthRateLimitOptions.ConcurrentPasswordChecksKey, "2"),
            (AuthRateLimitOptions.QueuedPasswordChecksKey, "0"),
            (AuthRateLimitOptions.BodyReadTimeoutSecondsKey, "3")));

        Assert.Equal((5, 2, 0), (limits.AttemptsPerMinute, limits.ConcurrentPasswordChecks, limits.QueuedPasswordChecks));
        Assert.Equal(TimeSpan.FromSeconds(3), limits.BodyReadTimeout);
    }

    [Theory]
    [InlineData(AuthRateLimitOptions.AttemptsPerMinuteKey, "0")]
    [InlineData(AuthRateLimitOptions.AttemptsPerMinuteKey, "-1")]
    [InlineData(AuthRateLimitOptions.ConcurrentPasswordChecksKey, "0")]
    [InlineData(AuthRateLimitOptions.QueuedPasswordChecksKey, "-1")]
    [InlineData(AuthRateLimitOptions.QueuedPasswordChecksKey, "lots")]
    [InlineData(AuthRateLimitOptions.BodyReadTimeoutSecondsKey, "0")]
    public void InvalidLimitsNameTheSetting(string key, string value)
    {
        var error = Assert.Throws<InvalidOperationException>(() =>
            AuthRateLimitOptions.FromConfiguration(TestConfiguration.From((key, value))));

        Assert.Contains(key, error.Message, StringComparison.Ordinal);
    }
}
