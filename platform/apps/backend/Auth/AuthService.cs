using Mosaic.Server.Data;

namespace Mosaic.Server.Auth;

public enum AuthOutcome
{
    Succeeded,
    MalformedBody,
    ValidationFailed,
    SetupAlreadyCompleted,
    InvalidSetupCode,
    SetupRequired,
    InvalidCredentials,
}

/// <summary>The result of an auth operation; <see cref="Session"/> is set only on success.</summary>
public sealed record AuthResult(
    AuthOutcome Outcome,
    Admin? Admin = null,
    IssuedSession? Session = null,
    IReadOnlyDictionary<string, string[]>? Errors = null)
{
    public static AuthResult Succeeded(Admin admin, IssuedSession session) => new(AuthOutcome.Succeeded, admin, session);

    public static AuthResult Failed(AuthOutcome outcome) => new(outcome);

    public static AuthResult Invalid(IReadOnlyDictionary<string, string[]> errors) => new(AuthOutcome.ValidationFailed, Errors: errors);
}

/// <summary>
/// First-run setup, login, and logout rules, independent of HTTP. The check order is part of
/// the Admin API Contract, so each method applies its checks in the documented sequence.
/// </summary>
public sealed class AuthService(
    MosaicDatabase database, AdminStore admins, SessionStore sessions, SetupCode setupCode, PasswordHasher hasher)
{
    public bool SetupRequired()
    {
        using var connection = database.OpenConnection();
        return !admins.Exists(connection);
    }

    /// <summary>Creates the admin and its first session atomically. A null request means the body was malformed.</summary>
    public AuthResult Setup(SetupRequest? request)
    {
        using var connection = database.OpenConnection();
        if (admins.Exists(connection))
        {
            return AuthResult.Failed(AuthOutcome.SetupAlreadyCompleted);
        }

        if (request is null)
        {
            return AuthResult.Failed(AuthOutcome.MalformedBody);
        }

        var errors = CredentialRules.ValidateSetup(request);
        if (errors.Count > 0)
        {
            return AuthResult.Invalid(errors);
        }

        if (!setupCode.Matches(request.SetupCode!))
        {
            return AuthResult.Failed(AuthOutcome.InvalidSetupCode);
        }

        // Hash before taking the write lock; it is the slow part.
        var passwordHash = hasher.Hash(request.Password!);

        // BEGIN IMMEDIATE serializes concurrent setups; the loser sees the CHECK (id = 1) conflict.
        using var transaction = connection.BeginTransaction(deferred: false);
        var admin = admins.TryCreate(transaction, request.Username!, passwordHash);
        if (admin is null)
        {
            return AuthResult.Failed(AuthOutcome.SetupAlreadyCompleted);
        }

        var session = sessions.Create(connection, admin.Id, transaction);
        transaction.Commit();
        return AuthResult.Succeeded(admin, session);
    }

    /// <summary>Checks credentials and issues a session. A null request means the body was malformed.</summary>
    public AuthResult Login(LoginRequest? request)
    {
        using var connection = database.OpenConnection();
        if (!admins.Exists(connection))
        {
            return AuthResult.Failed(AuthOutcome.SetupRequired);
        }

        if (request is null)
        {
            return AuthResult.Failed(AuthOutcome.MalformedBody);
        }

        var errors = CredentialRules.ValidateLogin(request);
        if (errors.Count > 0)
        {
            return AuthResult.Invalid(errors);
        }

        // Unknown usernames and wrong passwords take the same time and get the same result.
        var admin = admins.FindByUsername(connection, request.Username!);
        var verified = admin is null
            ? hasher.VerifyAgainstDummy(request.Password!)
            : hasher.Verify(request.Password!, admin.PasswordHash);
        if (!verified || admin is null)
        {
            return AuthResult.Failed(AuthOutcome.InvalidCredentials);
        }

        return AuthResult.Succeeded(admin, sessions.Create(connection, admin.Id));
    }

    /// <summary>Ends the session for <paramref name="token"/>, if any. Logout always succeeds.</summary>
    public void Logout(string? token)
    {
        if (string.IsNullOrEmpty(token))
        {
            return;
        }

        using var connection = database.OpenConnection();
        sessions.Delete(connection, token);
    }
}
