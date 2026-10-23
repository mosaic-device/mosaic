using Microsoft.AspNetCore.Http;
using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests.Auth;

/// <summary>The Origin check and session cookie, exercised without a server.</summary>
public sealed class RequestPipelineTests
{
    private static HttpRequest Request(string method, string path, string? origin = null)
    {
        var context = new DefaultHttpContext();
        context.Request.Method = method;
        context.Request.Path = path;
        if (origin is not null)
        {
            context.Request.Headers.Origin = origin;
        }

        return context.Request;
    }

    [Theory]
    [InlineData("POST", "/api/setup", null, true)]
    [InlineData("PUT", "/api/anything", null, true)]
    [InlineData("PATCH", "/api/anything", "https://evil.example", true)]
    [InlineData("DELETE", "/api/anything", null, true)]
    [InlineData("POST", "/api/setup", "https://mosaic.test", false)]
    [InlineData("GET", "/api/setup", null, false)]
    [InlineData("HEAD", "/api/setup", null, false)]
    [InlineData("POST", "/healthz", null, false)]
    [InlineData("POST", "/apiary", null, false)]
    public void OriginCheckAppliesToStateChangingApiRequests(string method, string path, string? origin, bool rejected) =>
        Assert.Equal(rejected, OriginCheck.Rejects(Request(method, path, origin), TestConfiguration.Options()));

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void CookieFollowsTheSecureSetting(bool secure)
    {
        var context = new DefaultHttpContext();

        new SessionCookie(TestConfiguration.Options(secureCookie: secure))
            .Append(context.Response, Issued(idle: TimeSpan.FromDays(7), absolute: TimeSpan.FromDays(30)));

        var header = context.Response.Headers.SetCookie.ToString().ToLowerInvariant();
        Assert.StartsWith("mosaic_session=token;", header, StringComparison.Ordinal);
        Assert.Equal(secure, header.Contains("; secure", StringComparison.Ordinal));
        Assert.Contains("; httponly", header, StringComparison.Ordinal);
        Assert.Contains("; samesite=strict", header, StringComparison.Ordinal);
        Assert.Contains("; max-age=604800", header, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(7.0, 30.0, 604800)]
    [InlineData(7.0, 2.0, 172800)] // Near the absolute expiry, the cookie must not outlive the session.
    [InlineData(2.0, 2.0, 172800)]
    [InlineData(7.0, 2.00001, 172800)] // Fractions of a second round down.
    public void CookieMaxAgeEndsWithTheSession(double idleDays, double absoluteDays, int maxAge)
    {
        var context = new DefaultHttpContext();

        new SessionCookie(TestConfiguration.Options())
            .Append(context.Response, Issued(TimeSpan.FromDays(idleDays), TimeSpan.FromDays(absoluteDays)));

        Assert.Contains($"; max-age={maxAge};", context.Response.Headers.SetCookie.ToString() + ";", StringComparison.Ordinal);
    }

    private static IssuedSession Issued(TimeSpan idle, TimeSpan absolute)
    {
        var now = new DateTimeOffset(2026, 10, 21, 12, 0, 0, TimeSpan.Zero);
        return new IssuedSession("token", now, now + idle, now + absolute);
    }

    [Fact]
    public void ClearingExpiresTheCookieImmediately()
    {
        var context = new DefaultHttpContext();

        new SessionCookie(TestConfiguration.Options()).Clear(context.Response);

        var header = context.Response.Headers.SetCookie.ToString().ToLowerInvariant();
        Assert.StartsWith("mosaic_session=;", header, StringComparison.Ordinal);
        Assert.Contains("; max-age=0", header, StringComparison.Ordinal);
        Assert.Contains("; path=/", header, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("mosaic_session=abc", true, "abc")]
    [InlineData("mosaic_session=", false, "")]
    [InlineData("other=abc", false, "")]
    [InlineData(null, false, "")]
    public void ReadingTheCookieIgnoresMissingAndEmptyValues(string? cookieHeader, bool found, string token)
    {
        var context = new DefaultHttpContext();
        if (cookieHeader is not null)
        {
            context.Request.Headers.Cookie = cookieHeader;
        }

        Assert.Equal(found, SessionCookie.TryRead(context.Request, out var read));
        Assert.Equal(token, read);
    }
}
