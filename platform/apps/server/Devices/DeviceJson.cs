using System.Text.Json;
using System.Text.Json.Serialization;

namespace Mosaic.Server.Devices;

/// <summary>
/// <c>POST /api/devices/claim</c>. <see cref="Code"/> is read as any JSON value, so a missing or
/// non-string code is a field error under <c>code</c> rather than a malformed body.
/// </summary>
public sealed record ClaimRequest(JsonElement Code);

/// <summary>A paired device (Admin API Contract, section 5). <see cref="PairedAt"/> is UTC, in whole seconds.</summary>
public sealed record DeviceSummary(long Id, DateTime PairedAt, bool Connected);

public sealed record DeviceResponse(DeviceSummary Device);

/// <summary><c>GET /api/devices</c>: paired devices, sorted by <see cref="DeviceSummary.Id"/>.</summary>
public sealed record DeviceListResponse(IReadOnlyList<DeviceSummary> Devices);

// Source-generated serialization keeps the API Native-AOT-compatible.
[JsonSourceGenerationOptions(JsonSerializerDefaults.Web)]
[JsonSerializable(typeof(ClaimRequest))]
[JsonSerializable(typeof(DeviceResponse))]
[JsonSerializable(typeof(DeviceListResponse))]
public sealed partial class DeviceJsonContext : JsonSerializerContext;
