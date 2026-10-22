namespace Mosaic.Server.Auth;

/// <summary>Origin and cookie settings for the admin API.</summary>
public sealed class AuthOptions
{
    public const string PublicOriginKey = "Auth:PublicOrigin";
    public const string AdditionalOriginsKey = "Auth:AdditionalOrigins";
    public const string SecureCookieKey = "Auth:SecureCookie";

    private AuthOptions(string publicOrigin, IReadOnlyList<string> allowedOrigins, bool secureCookie)
    {
        PublicOrigin = publicOrigin;
        AllowedOrigins = allowedOrigins;
        SecureCookie = secureCookie;
    }

    /// <summary>The origin browsers use to reach the server, such as <c>https://mosaic.example</c>.</summary>
    public string PublicOrigin { get; }

    /// <summary>Origins accepted by the Origin check: the public origin plus any additional ones.</summary>
    public IReadOnlyList<string> AllowedOrigins { get; }

    /// <summary>Whether the session cookie is marked <c>Secure</c>. Comes from configuration, not the request scheme.</summary>
    public bool SecureCookie { get; }

    public static AuthOptions FromConfiguration(IConfiguration configuration)
    {
        var configured = configuration[PublicOriginKey];
        if (string.IsNullOrWhiteSpace(configured))
        {
            throw new InvalidOperationException(
                $"Set {PublicOriginKey} (environment variable Auth__PublicOrigin) to the origin browsers use to reach the server, for example https://mosaic.example.");
        }

        var publicOrigin = NormalizeOrigin(configured, PublicOriginKey);
        var allowed = new List<string> { publicOrigin };
        foreach (var child in configuration.GetSection(AdditionalOriginsKey).GetChildren())
        {
            if (!string.IsNullOrWhiteSpace(child.Value))
            {
                allowed.Add(NormalizeOrigin(child.Value, AdditionalOriginsKey));
            }
        }

        var secure = configuration[SecureCookieKey];
        return new AuthOptions(publicOrigin, allowed, string.IsNullOrWhiteSpace(secure) || bool.Parse(secure));
    }

    public bool IsAllowedOrigin(string? origin) =>
        !string.IsNullOrEmpty(origin)
        && AllowedOrigins.Any(allowed => string.Equals(allowed, origin, StringComparison.OrdinalIgnoreCase));

    private static string NormalizeOrigin(string value, string key)
    {
        if (!Uri.TryCreate(value.Trim(), UriKind.Absolute, out var uri)
            || (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps)
            || uri.AbsolutePath != "/" || uri.Query.Length > 0)
        {
            throw new InvalidOperationException($"{key} must be an origin such as https://mosaic.example, not '{value}'.");
        }

        // Browsers omit default ports from Origin, and so does GetLeftPart.
        return uri.GetLeftPart(UriPartial.Authority);
    }
}
