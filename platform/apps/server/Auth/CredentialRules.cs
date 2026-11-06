namespace Mosaic.Server.Auth;

/// <summary>The contract's field rules (Admin API Contract, section 2). Pure functions, no I/O.</summary>
public static class CredentialRules
{
    public const int MinUsernameLength = 3;
    public const int MaxUsernameLength = 32;
    public const int MinPasswordLength = 12;
    public const int MaxPasswordLength = 128;

    private const string Required = "Required.";

    /// <summary>Every field problem in a setup request, keyed by field name; empty when valid.</summary>
    public static IReadOnlyDictionary<string, string[]> ValidateSetup(SetupRequest request)
    {
        var errors = new Dictionary<string, string[]>();
        AddIfInvalid(errors, "username", UsernameError(request.Username));
        AddIfInvalid(errors, "password", PasswordError(request.Password));
        AddIfInvalid(errors, "setupCode", string.IsNullOrWhiteSpace(request.SetupCode) ? Required : null);
        return errors;
    }

    /// <summary>
    /// Login only requires both fields. Applying the setup rules here would reveal the
    /// password policy to anyone guessing and adds nothing: a wrong value fails anyway.
    /// </summary>
    public static IReadOnlyDictionary<string, string[]> ValidateLogin(LoginRequest request)
    {
        var errors = new Dictionary<string, string[]>();
        AddIfInvalid(errors, "username", string.IsNullOrEmpty(request.Username) ? Required : null);
        AddIfInvalid(errors, "password", string.IsNullOrEmpty(request.Password) ? Required : null);
        return errors;
    }

    private static string? UsernameError(string? username) => username switch
    {
        null or "" => Required,
        { Length: < MinUsernameLength or > MaxUsernameLength } => $"Must be {MinUsernameLength} to {MaxUsernameLength} characters.",
        _ when !username.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-') => "Use only letters, digits, '.', '_' or '-'.",
        _ => null,
    };

    private static string? PasswordError(string? password) => password switch
    {
        null or "" => Required,
        { Length: < MinPasswordLength } => $"Must be at least {MinPasswordLength} characters.",
        { Length: > MaxPasswordLength } => $"Must be at most {MaxPasswordLength} characters.",
        _ => null,
    };

    private static void AddIfInvalid(Dictionary<string, string[]> errors, string field, string? message)
    {
        if (message is not null)
        {
            errors[field] = [message];
        }
    }
}
