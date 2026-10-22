using System.Text.Json;
using System.Text.Json.Serialization;

namespace Mosaic.Server.Auth;

public sealed record SetupRequest(string? Username, string? Password, string? SetupCode);

public sealed record LoginRequest(string? Username, string? Password);

public sealed record SetupStatusResponse(bool SetupRequired);

public sealed record AdminSummary(string Username);

public sealed record AdminResponse(AdminSummary Admin);

/// <summary>RFC 9457 Problem Details plus the contract's machine-readable <c>code</c>.</summary>
public sealed record ApiProblem(
    int Status,
    string Code,
    string Title,
    string? TraceId,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] IReadOnlyDictionary<string, string[]>? Errors = null);

// Source-generated serialization keeps the API Native-AOT-compatible.
[JsonSourceGenerationOptions(JsonSerializerDefaults.Web)]
[JsonSerializable(typeof(SetupRequest))]
[JsonSerializable(typeof(LoginRequest))]
[JsonSerializable(typeof(SetupStatusResponse))]
[JsonSerializable(typeof(AdminResponse))]
[JsonSerializable(typeof(ApiProblem))]
public sealed partial class AuthJsonContext : JsonSerializerContext;
