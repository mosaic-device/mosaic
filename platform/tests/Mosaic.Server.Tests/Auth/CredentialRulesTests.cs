using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests.Auth;

public sealed class CredentialRulesTests
{
    private const string ValidPassword = "mosaic-desk-2026";

    private static IReadOnlyDictionary<string, string[]> Setup(string? username = "Bridger", string? password = ValidPassword, string? code = "K7QF-2M9X") =>
        CredentialRules.ValidateSetup(new SetupRequest(username, password, code));

    [Theory]
    [InlineData("abc")]
    [InlineData("Bridger")]
    [InlineData("a.b_c-9")]
    [InlineData("abcdefghijklmnopqrstuvwxyz012345")] // 32 characters
    public void ValidUsernamesPass(string username) => Assert.Empty(Setup(username));

    [Theory]
    [InlineData(null, "Required.")]
    [InlineData("", "Required.")]
    [InlineData("ab", "Must be 3 to 32 characters.")]
    [InlineData("abcdefghijklmnopqrstuvwxyz0123456", "Must be 3 to 32 characters.")]
    [InlineData("has space", "Use only letters, digits, '.', '_' or '-'.")]
    [InlineData("ünïcode", "Use only letters, digits, '.', '_' or '-'.")]
    [InlineData("name@host", "Use only letters, digits, '.', '_' or '-'.")]
    public void InvalidUsernamesExplainTheRule(string? username, string message) =>
        Assert.Equal([message], Assert.Single(Setup(username)).Value);

    [Theory]
    [InlineData(null, "Required.")]
    [InlineData("", "Required.")]
    [InlineData("elevenchars", "Must be at least 12 characters.")]
    public void ShortPasswordsAreRejected(string? password, string message) =>
        Assert.Equal(new KeyValuePair<string, string[]>("password", [message]), Assert.Single(Setup(password: password)));

    [Fact]
    public void PasswordLengthLimitsAreInclusive()
    {
        Assert.Empty(Setup(password: new string('p', CredentialRules.MinPasswordLength)));
        Assert.Empty(Setup(password: new string('p', CredentialRules.MaxPasswordLength)));
        Assert.Equal(["Must be at most 128 characters."], Setup(password: new string('p', CredentialRules.MaxPasswordLength + 1))["password"]);
    }

    [Fact]
    public void PasswordsHaveNoCompositionRules() => Assert.Empty(Setup(password: "aaaaaaaaaaaa"));

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    public void SetupCodeIsRequired(string? code) =>
        Assert.Equal(new KeyValuePair<string, string[]>("setupCode", ["Required."]), Assert.Single(Setup(code: code)));

    [Fact]
    public void EveryFieldProblemIsReportedAtOnce() =>
        Assert.Equal(["password", "setupCode", "username"], Setup("x", "short", "").Keys.Order());

    [Fact]
    public void LoginOnlyRequiresBothFields()
    {
        Assert.Empty(CredentialRules.ValidateLogin(new LoginRequest("x", "short")));
        Assert.Equal(["password", "username"], CredentialRules.ValidateLogin(new LoginRequest(null, "")).Keys.Order());
    }
}
