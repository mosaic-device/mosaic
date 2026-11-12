using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.RateLimiting;
using Mosaic.Device.V1;
using Mosaic.Server.Auth;

namespace Mosaic.Server.Devices;

/// <summary>
/// The admin app's device endpoints (Admin API Contract, section 5). Every one needs a session,
/// and the Origin check runs before it, so a claim is checked in contract order: Origin, session,
/// attempts per client, content type, body size, the code's format, then the code itself. Listing,
/// removal and test notifications are not rate-limited; a device takes one notification at a time.
/// </summary>
public static partial class DeviceApi
{
    public const string ListPath = "/api/devices";

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
        app.MapGet(ListPath, List).RequireAdminSession();
        app.MapPost(ClaimPath, ClaimAsync).RequireAdminSession().RequireRateLimiting(ClaimAttemptsPolicy);
        // An ID that isn't a whole number matches no route and gets the empty 404.
        app.MapDelete(ListPath + "/{id:long}", Remove).RequireAdminSession();
        app.MapPost(ListPath + "/{id:long}/test-notification", SendTestNotificationAsync).RequireAdminSession();
    }

    /// <summary>
    /// Sends the fixed test notification and waits for its receipt, for up to
    /// <see cref="NotificationDelivery.ReceiptTimeout"/>. Checked as the contract orders it:
    /// Origin and session first (middleware), then the device, then whether one is already waiting.
    /// </summary>
    /// <remarks>
    /// An offline or silent device is a 200; <c>outcome</c> says what happened. The wait doesn't
    /// end when the client goes away, so the device can't be sent a second one too early.
    /// </remarks>
    private static async Task<IResult> SendTestNotificationAsync(
        HttpContext context, long id, DeviceStore devices, NotificationDelivery delivery, TimeProvider time)
    {
        var arrived = time.GetUtcNow();
        if (devices.FindPaired(id) is not { } secretHash)
        {
            return Problems.DeviceNotFound(context);
        }

        var start = delivery.Start(id, secretHash, TestNotification(arrived));
        if (start.Kind == DeliveryStartKind.Busy)
        {
            return Problems.TooManyRequests(context, start.RetryAfterSeconds);
        }

        var outcome = start.Kind == DeliveryStartKind.Offline ? NotificationOutcome.Offline : await start.Outcome!;
        return Results.Json(new TestNotificationResponse(OutcomeName(outcome)), DeviceJsonContext.Default.TestNotificationResponse);
    }

    /// <summary>The notification the contract fixes for a test, with a new ID for each send.</summary>
    public static Notification TestNotification(DateTimeOffset arrived) => new()
    {
        Id = "test-" + Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(8)),
        Priority = NotificationPriority.Normal,
        Source = "Mosaic",
        Title = "Test notification",
        Text = "Sent from the admin app.",
        OccurredAt = arrived.ToUnixTimeSeconds(),
    };

    private static string OutcomeName(NotificationOutcome outcome) => outcome switch
    {
        NotificationOutcome.Delivered => "delivered",
        NotificationOutcome.Offline => "offline",
        NotificationOutcome.Rejected => "rejected",
        _ => "no_answer",
    };

    /// <summary><c>connected</c> is whether the credential has an open connection right now.</summary>
    private static IResult List(DeviceStore devices, DeviceConnections connections) =>
        Results.Json(
            new DeviceListResponse(devices.ListPaired()
                .Select(device => new DeviceSummary(
                    device.Id, Timestamps.WholeSeconds(device.PairedAt), connections.IsConnected(device.SecretHash)))
                .ToList()),
            DeviceJsonContext.Default.DeviceListResponse);

    /// <summary>
    /// Revokes the device, then tells its connection, if any, which sends <c>CREDENTIAL_REVOKED</c>
    /// and closes. The handshake registers a connection before reading its credential, so a device
    /// reconnecting during removal either reads the revocation or is registered in time to be told.
    /// </summary>
    private static IResult Remove(HttpContext context, long id, DeviceStore devices, DeviceConnections connections, ILoggerFactory loggers)
    {
        if (devices.Revoke(id) is not { } secretHash)
        {
            return Problems.DeviceNotFound(context);
        }

        LogRemoved(loggers.CreateLogger(typeof(DeviceApi)), id);
        connections.NotifyRevoked(secretHash);
        return Results.NoContent();
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "Removed device {DeviceId}")]
    private static partial void LogRemoved(ILogger logger, long deviceId);

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

        if (!PairingCodes.TryNormalize(ReadString(body.Value.Code), out var code))
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

    /// <summary>
    /// The field as a string, or null when it isn't one. A JSON string can escape a lone UTF-16
    /// surrogate, which parses but can't be decoded; that is a badly formed code, not a failure.
    /// </summary>
    private static string? ReadString(JsonElement field)
    {
        if (field.ValueKind != JsonValueKind.String)
        {
            return null;
        }

        try
        {
            return field.GetString();
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }
}
