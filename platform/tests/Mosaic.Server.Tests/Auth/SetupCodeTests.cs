using Mosaic.Server.Auth;

namespace Mosaic.Server.Tests.Auth;

public sealed class SetupCodeTests
{
    private static SetupCode Override(string value) =>
        SetupCode.FromConfiguration(TestConfiguration.From((SetupCode.OverrideKey, value)));

    [Fact]
    public void GeneratedCodeIsEightUnambiguousCharacters()
    {
        var code = SetupCode.FromConfiguration(TestConfiguration.From());

        Assert.False(code.FromOverride);
        Assert.Matches("^[A-HJ-NP-Z2-9]{4}-[A-HJ-NP-Z2-9]{4}$", code.Display);
        Assert.True(code.Matches(code.Display));
    }

    [Fact]
    public void EachStartGeneratesANewCode()
    {
        var codes = Enumerable.Range(0, 20).Select(_ => SetupCode.FromConfiguration(TestConfiguration.From()).Display);

        Assert.Equal(20, codes.Distinct().Count());
    }

    [Theory]
    [InlineData("K7QF-2M9X", true)]
    [InlineData("k7qf-2m9x", true)]
    [InlineData("K7QF2M9X", true)]
    [InlineData("  k7qf2m9x  ", true)]
    [InlineData("K7QF-2M9Y", false)]
    [InlineData("K7QF", false)]
    [InlineData("K7QF-2M9X0", false)]
    [InlineData("", false)]
    public void MatchingIgnoresCaseWhitespaceAndTheDash(string candidate, bool matches) =>
        Assert.Equal(matches, Override("K7QF-2M9X").Matches(candidate));

    [Fact]
    public void OverrideIsUsedInsteadOfAGeneratedCode()
    {
        var code = Override(" K7QF-2M9X ");

        Assert.True(code.FromOverride);
        Assert.Equal("K7QF-2M9X", code.Display);
    }

    [Theory]
    [InlineData("-")]
    [InlineData("---")]
    public void OverrideMustContainACharacter(string value) =>
        Assert.Throws<InvalidOperationException>(() => Override(value));
}
