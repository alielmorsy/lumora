#include <gtest/gtest.h>
#include <lumora/Style/Css.h>

using namespace lumora::style;
using lumora::StringId;

namespace
{
    [[nodiscard]] StringId Sid(const char* s) { return StringId(std::string_view(s)); }
}

TEST(StyleParserTests, BasicRuleAndDeclaration)
{
    const Stylesheet s = ParseStylesheet(".card { display: flex; border-radius: 8px; }");
    EXPECT_EQ(s.rules.size(), 1u);
    EXPECT_EQ(s.rules[0].selectors.size(), 1u);
    const Selector& sel = s.rules[0].selectors[0];
    EXPECT_EQ(sel.compounds.size(), 1u);
    EXPECT_EQ(sel.compounds[0].classes.size(), 1u);
    EXPECT_EQ(sel.compounds[0].classes[0], Sid("card"));
    EXPECT_EQ(sel.specificity, 1u);
    EXPECT_EQ(s.rules[0].declarations.size(), 2u);
    EXPECT_EQ(s.rules[0].declarations[0].prop, Sid("display"));
    EXPECT_EQ(s.rules[0].declarations[0].value.kind, ValueKind::Keyword);
    EXPECT_EQ(s.rules[0].declarations[0].value.keyword, Sid("flex"));
    EXPECT_EQ(s.rules[0].declarations[1].prop, Sid("border-radius"));
    EXPECT_EQ(s.rules[0].declarations[1].value.kind, ValueKind::Length);
    EXPECT_EQ(s.rules[0].declarations[1].value.unit, Unit::Px);
    EXPECT_FLOAT_EQ(s.rules[0].declarations[1].value.num, 8.f);
}

TEST(StyleParserTests, CompoundAndCombinators)
{
    {
        const Stylesheet s = ParseStylesheet(".a.b { color: red }");
        EXPECT_EQ(s.rules.size(), 1u);
        const Selector& sel = s.rules[0].selectors[0];
        EXPECT_EQ(sel.compounds.size(), 1u);
        EXPECT_EQ(sel.compounds[0].classes.size(), 2u);
        EXPECT_EQ(sel.specificity, 2u);
    }
    {
        const Stylesheet s = ParseStylesheet(".a .b { color: red }");
        const Selector& sel = s.rules[0].selectors[0];
        EXPECT_EQ(sel.compounds.size(), 2u);
        EXPECT_EQ(sel.combinators.size(), 1u);
        EXPECT_EQ(sel.combinators[0], Combinator::Descendant);
    }
    {
        const Stylesheet s = ParseStylesheet(".a > .b { color: red }");
        const Selector& sel = s.rules[0].selectors[0];
        EXPECT_EQ(sel.compounds.size(), 2u);
        EXPECT_EQ(sel.combinators.size(), 1u);
        EXPECT_EQ(sel.combinators[0], Combinator::Child);
    }
}

TEST(StyleParserTests, SelectorList)
{
    const Stylesheet s = ParseStylesheet(".a, .b:hover { color: blue }");
    EXPECT_EQ(s.rules.size(), 1u);
    EXPECT_EQ(s.rules[0].selectors.size(), 2u);
    EXPECT_EQ(s.rules[0].selectors[0].compounds[0].classes[0], Sid("a"));
    EXPECT_TRUE(HasPseudo(s.rules[0].selectors[1].compounds[0].pseudo, Pseudo::Hover));
    EXPECT_EQ(s.rules[0].selectors[1].specificity, 2u);
}

TEST(StyleParserTests, PseudoClasses)
{
    const Stylesheet s = ParseStylesheet(
        ".btn:hover:focus { color: red } .x:disabled { color: red } .c:checked { color: red }");
    EXPECT_EQ(s.rules.size(), 3u);
    const auto& p0 = s.rules[0].selectors[0].compounds[0].pseudo;
    EXPECT_TRUE(HasPseudo(p0, Pseudo::Hover));
    EXPECT_TRUE(HasPseudo(p0, Pseudo::Focus));
    EXPECT_TRUE(HasPseudo(s.rules[1].selectors[0].compounds[0].pseudo, Pseudo::Disabled));
    EXPECT_TRUE(HasPseudo(s.rules[2].selectors[0].compounds[0].pseudo, Pseudo::Checked));
}

TEST(StyleParserTests, NestingDescendantImplicit)
{
    const Stylesheet s = ParseStylesheet(".card { color: red; .title { color: blue } }");
    EXPECT_EQ(s.rules.size(), 2u);
    const Rule* nested = nullptr;
    const Rule* outer = nullptr;
    for (const Rule& r : s.rules)
    {
        if (r.selectors[0].compounds.size() == 2) nested = &r;
        else outer = &r;
    }
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(nested->selectors[0].compounds.size(), 2u);
    EXPECT_EQ(nested->selectors[0].compounds[0].classes[0], Sid("card"));
    EXPECT_EQ(nested->selectors[0].compounds[1].classes[0], Sid("title"));
    EXPECT_EQ(nested->selectors[0].combinators[0], Combinator::Descendant);
    EXPECT_LT(outer->order, nested->order);
}

TEST(StyleParserTests, NestingAmpersand)
{
    {
        const Stylesheet s = ParseStylesheet(".card { &:hover { color: red } }");
        const Rule* nested = nullptr;
        for (const Rule& r : s.rules)
            if (!r.declarations.empty()) nested = &r;
        ASSERT_NE(nested, nullptr);
        const Selector& sel = nested->selectors[0];
        EXPECT_EQ(sel.compounds.size(), 1u);
        EXPECT_EQ(sel.compounds[0].classes[0], Sid("card"));
        EXPECT_TRUE(HasPseudo(sel.compounds[0].pseudo, Pseudo::Hover));
    }
    {
        const Stylesheet s = ParseStylesheet(".card { & .child { color: red } }");
        const Rule* nested = nullptr;
        for (const Rule& r : s.rules)
            if (r.selectors[0].compounds.size() == 2) nested = &r;
        ASSERT_NE(nested, nullptr);
        EXPECT_EQ(nested->selectors[0].compounds[0].classes[0], Sid("card"));
        EXPECT_EQ(nested->selectors[0].compounds[1].classes[0], Sid("child"));
    }
}

TEST(StyleParserTests, MediaQuery)
{
    const Stylesheet s = ParseStylesheet("@media (max-width: 600px) { .a { color: red } }");
    EXPECT_EQ(s.media.size(), 1u);
    EXPECT_EQ(s.media[0].condition.features.size(), 1u);
    EXPECT_EQ(s.media[0].condition.features[0].type, MediaFeature::Type::MaxWidth);
    EXPECT_FLOAT_EQ(s.media[0].condition.features[0].value, 600.f);
    EXPECT_EQ(s.media[0].rules.size(), 1u);
    EXPECT_EQ(s.media[0].rules[0].selectors[0].compounds[0].classes[0], Sid("a"));
}

TEST(StyleParserTests, Keyframes)
{
    const Stylesheet s = ParseStylesheet("@keyframes spin { from { opacity: 0 } 50% { opacity: .5 } to { opacity: 1 } }");
    EXPECT_EQ(s.keyframes.size(), 1u);
    auto it = s.keyframes.find(Sid("spin"));
    ASSERT_NE(it, s.keyframes.end());
    EXPECT_EQ(it->second.stops.size(), 3u);
    EXPECT_FLOAT_EQ(it->second.stops[0].offset, 0.f);
    EXPECT_FLOAT_EQ(it->second.stops[1].offset, 0.5f);
    EXPECT_FLOAT_EQ(it->second.stops[2].offset, 1.f);
}

TEST(StyleParserTests, RootVarsAndVarRef)
{
    const Stylesheet s = ParseStylesheet(":root { --brand: #ffffff; } .a { color: var(--brand, inherit); }");
    EXPECT_EQ(s.rootVars.size(), 1u);
    EXPECT_EQ(s.rootVars[0].prop, Sid("--brand"));
    EXPECT_EQ(s.rootVars[0].value.kind, ValueKind::Color);
    EXPECT_EQ(s.rules.size(), 1u);
    const CssValue& v = s.rules[0].declarations[0].value;
    EXPECT_EQ(v.kind, ValueKind::VarRef);
    EXPECT_EQ(v.keyword, Sid("--brand"));
    EXPECT_EQ(v.list.size(), 1u);
    EXPECT_EQ(v.list[0].kind, ValueKind::Keyword);
    EXPECT_EQ(v.list[0].keyword, Sid("inherit"));
}

TEST(StyleParserTests, UnitsPreservedAndImportant)
{
    const Stylesheet s = ParseStylesheet(".a { width: 50%; margin: 2em; opacity: 0.5; color: red !important; }");
    const auto& d = s.rules[0].declarations;
    EXPECT_EQ(d.size(), 4u);
    EXPECT_TRUE(d[0].value.kind == ValueKind::Length && d[0].value.unit == Unit::Percent && d[0].value.num == 50.f);
    EXPECT_TRUE(d[1].value.kind == ValueKind::Length && d[1].value.unit == Unit::Em && d[1].value.num == 2.f);
    EXPECT_TRUE(d[2].value.kind == ValueKind::Number && d[2].value.num == 0.5f);
    EXPECT_TRUE(d[3].important);
}

TEST(StyleParserTests, ValueLists)
{
    {
        const Stylesheet s = ParseStylesheet(".a { padding: 10px 20px; }");
        const CssValue& v = s.rules[0].declarations[0].value;
        EXPECT_EQ(v.kind, ValueKind::List);
        EXPECT_FALSE(v.comma);
        EXPECT_EQ(v.list.size(), 2u);
        EXPECT_FLOAT_EQ(v.list[0].num, 10.f);
        EXPECT_FLOAT_EQ(v.list[1].num, 20.f);
    }
    {
        const Stylesheet s = ParseStylesheet(".a { transition: color 0.3s, background-color 0.2s; }");
        const CssValue& v = s.rules[0].declarations[0].value;
        EXPECT_EQ(v.kind, ValueKind::List);
        EXPECT_TRUE(v.comma);
        EXPECT_EQ(v.list.size(), 2u);
        EXPECT_EQ(v.list[0].kind, ValueKind::List);
        EXPECT_FALSE(v.list[0].comma);
        EXPECT_EQ(v.list[0].list.size(), 2u);
        EXPECT_EQ(v.list[0].list[1].kind, ValueKind::Time);
        EXPECT_FLOAT_EQ(v.list[0].list[1].AsSeconds(), 0.3f);
    }
}

TEST(StyleParserTests, ColorHexAndFunctions)
{
    {
        const Stylesheet s = ParseStylesheet(".a { color: #ff0000; }");
        const CssValue& v = s.rules[0].declarations[0].value;
        EXPECT_EQ(v.kind, ValueKind::Color);
        EXPECT_EQ(v.color.r, 255);
        EXPECT_EQ(v.color.g, 0);
        EXPECT_EQ(v.color.b, 0);
    }
    {
        const Stylesheet s = ParseStylesheet(".a { background-color: rgba(255, 128, 0, 0.5); }");
        const CssValue& v = s.rules[0].declarations[0].value;
        EXPECT_EQ(v.kind, ValueKind::Color);
        EXPECT_EQ(v.color.r, 255);
        EXPECT_EQ(v.color.g, 128);
        EXPECT_EQ(v.color.b, 0);
        EXPECT_EQ(v.color.a, 128);
    }
    {
        const Stylesheet s = ParseStylesheet(".a { transition-timing-function: cubic-bezier(0.4, 0, 0.2, 1); }");
        const CssValue& v = s.rules[0].declarations[0].value;
        EXPECT_EQ(v.kind, ValueKind::Function);
        EXPECT_EQ(v.keyword, Sid("cubic-bezier"));
        EXPECT_EQ(v.list.size(), 4u);
        EXPECT_FLOAT_EQ(v.list[0].num, 0.4f);
    }
}

TEST(StyleParserTests, KeywordsAreCaseInsensitive)
{
    const Stylesheet s = ParseStylesheet(".a { JUSTIFY-CONTENT: Center; }");
    EXPECT_EQ(s.rules[0].declarations[0].prop, Sid("justify-content"));
    EXPECT_EQ(s.rules[0].declarations[0].value.keyword, Sid("center"));
}

TEST(StyleParserTests, GridUnitsAndLines)
{
    {
        const Stylesheet s = ParseStylesheet(".a { grid-template-columns: 1fr 2fr 100px; }");
        const CssValue& v = s.rules[0].declarations[0].value;
        EXPECT_TRUE(v.kind == ValueKind::List && !v.comma && v.list.size() == 3);
        EXPECT_TRUE(v.list[0].kind == ValueKind::Length && v.list[0].unit == Unit::Fr && v.list[0].num == 1.f);
        EXPECT_EQ(v.list[1].unit, Unit::Fr);
        EXPECT_FLOAT_EQ(v.list[1].num, 2.f);
        EXPECT_EQ(v.list[2].unit, Unit::Px);
        EXPECT_FLOAT_EQ(v.list[2].num, 100.f);
    }
    {
        const Stylesheet s = ParseStylesheet(".a { grid-column: 2 / span 3; }");
        const CssValue& v = s.rules[0].declarations[0].value;
        EXPECT_TRUE(v.kind == ValueKind::List && v.list.size() == 4);
        EXPECT_TRUE(v.list[0].kind == ValueKind::Number && v.list[0].num == 2.f);
        EXPECT_EQ(v.list[1].keyword, Sid("/"));
        EXPECT_EQ(v.list[2].keyword, Sid("span"));
        EXPECT_TRUE(v.list[3].kind == ValueKind::Number && v.list[3].num == 3.f);
    }
}

TEST(StyleParserTests, MalformedRecovers)
{
    const Stylesheet s = ParseStylesheet(".a { color: ; broken } .b { color: red }");
    bool sawB = false;
    for (const Rule& r : s.rules)
        if (!r.selectors.empty() && !r.selectors[0].compounds.empty() &&
            !r.selectors[0].compounds[0].classes.empty() &&
            r.selectors[0].compounds[0].classes[0] == Sid("b"))
            sawB = true;
    EXPECT_TRUE(sawB);
}
