using Microsoft.Extensions.Configuration;
using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests.Auth;

internal static class TestConfiguration
{
    public static IConfiguration From(params (string Key, string? Value)[] settings) =>
        new ConfigurationBuilder()
            .AddInMemoryCollection(settings.Select(setting => new KeyValuePair<string, string?>(setting.Key, setting.Value)))
            .Build();

    public static AuthOptions Options(string publicOrigin = "https://mosaic.test", bool secureCookie = true, params string[] additionalOrigins) =>
        AuthOptions.FromConfiguration(From(
        [
            (AuthOptions.PublicOriginKey, publicOrigin),
            (AuthOptions.SecureCookieKey, secureCookie.ToString()),
            .. additionalOrigins.Select((origin, i) => ($"{AuthOptions.AdditionalOriginsKey}:{i}", (string?)origin)),
        ]));
}
