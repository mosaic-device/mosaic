using System.Text.Json;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.RateLimiting;
using Mosaic.Server.Auth;

namespace Mosaic.Server.Devices;

/// <summary>
/// The admin app's device endpoints (Admin API Contract, section 5). Every one needs a session,
/// and the Origin check runs before it, so a claim is checked in contract order: Origin, session,
/// attempts per client, content type, body size, the code's format, then the code itself.
/// </summary>
public static class DeviceApi
{
    public const string ClaimPath = "/api/devices/claim";

    public const string ClaimAttemptsPolicy = "device-claim-attempts";

    private const string CodeMessage = "Enter the 8-character code shown on the device.";

    private static readonly TimeSpan AttemptWindow = TimeSpan.FromMinutes(1);

    /// <summary>
    /// Adds the claim limit. It is checked after the session, so requests without one never use
    /// it up, and counted per client address like the setup and login limit, but separately.
    /// </summary>
    public static IServiceCollection AddDeviceApi(this IServiceCollection services)
    {
        services.AddRateLimiter(_ => { });
        services.AddOptions<RateLimiterOptions>().Configure<DeviceConnectionOptions>((options, devices) =>
            options.AddPolicy(ClaimAttemptsPolicy, context => RateLimitPartition.GetFixedWindowLimiter(
                context.Connection.RemoteIpAddress?.ToString() ?? "unknown",
                _ => new FixedWindowRateLimiterOptions
                {
                    PermitLimit = devices.ClaimAttemptsPerMinute,
                    Window = AttemptWindow,
                    QueueLimit = 0,
                })));
        return services;
    }

    public static void MapDeviceApi(this IEndpointRouteBuilder app)
    {
        app.MapPost(ClaimPath, ClaimAsync).RequireAdminSession().RequireRateLimiting(ClaimAttemptsPolicy);
    }

    private static async Task<IResult> ClaimAsync(HttpContext context, DevicePairing pairing, AuthRateLimitOptions limits)
    {
        if (!JsonBody.IsSupported(context.Request))
        {
            return Results.StatusCode(StatusCodes.Status415UnsupportedMediaType);
        }

        var body = await JsonBody.ReadAsync(context, DeviceJsonContext.Default.ClaimRequest, limits.BodyReadTimeout);
        if (body.Rejection is { } rejection)
        {
            return rejection;
        }

        if (body.Value is null)
        {
            return Problems.ValidationFailed(context, errors: null, "The request body must be a JSON object.");
        }

        var typed = body.Value.Code.ValueKind == JsonValueKind.String ? body.Value.Code.GetString() : null;
        if (!PairingCodes.TryNormalize(typed, out var code))
        {
            return Problems.ValidationFailed(context, new Dictionary<string, string[]> { ["code"] = [CodeMessage] });
        }

        var result = pairing.Claim(code);
        return result.Outcome switch
        {
            ClaimOutcome.Paired => Results.Json(
                new DeviceResponse(new DeviceSummary(
                    result.Device!.Id, Timestamps.WholeSeconds(result.Device.PairedAt), result.Connected)),
                DeviceJsonContext.Default.DeviceResponse),
            ClaimOutcome.Used => Problems.PairingCodeUsed(context),
            ClaimOutcome.Expired => Problems.PairingCodeExpired(context),
            _ => Problems.InvalidPairingCode(context),
        };
    }
}
