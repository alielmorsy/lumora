#include <gtest/gtest.h>
#include "Style/Cascade.h"

#include <lumora/Style/Css.h>

#include <vector>

using namespace lumora::style;
using lumora::StringId;

namespace
{
    [[nodiscard]] StringId Sid(const char* s) { return StringId(std::string_view(s)); }

    struct TestNode
    {
        NodeHandle parent = 0;
        std::vector<StringId> classes;
        PseudoMask pseudo = 0;
        bool root = false;
    };

    struct Tree
    {
        std::vector<TestNode> nodes{TestNode{}};

        NodeHandle Add(NodeHandle parent, std::vector<StringId> classes, PseudoMask pseudo = 0, bool root = false)
        {
            nodes.push_back(TestNode{parent, std::move(classes), pseudo, root});
            return static_cast<NodeHandle>(nodes.size() - 1);
        }

        [[nodiscard]] MatchOps Ops() const
        {
            return MatchOps{
                [this](NodeHandle h) { return nodes[h].parent; },
                [this](NodeHandle h) -> const std::vector<StringId>& { return nodes[h].classes; },
                [this](NodeHandle h) { return nodes[h].pseudo; },
                [this](NodeHandle h) { return nodes[h].root; },
            };
        }
    };

    [[nodiscard]] bool AlignIs(const ComputedStyle& cs, const char* kw)
    {
        const CssValue* v = cs.Get(Sid("align-items"));
        return v && v->kind == ValueKind::Keyword && v->keyword == Sid(kw);
    }
} // namespace

TEST(StyleMatchTests, SimpleClassMatch)
{
    const Stylesheet s = ParseStylesheet(".a { align-items: center } .other { align-items: stretch }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle n = t.Add(0, {Sid("a")});
    const ComputedStyle cs = Cascade(n, t.Ops(), idx);
    EXPECT_TRUE(AlignIs(cs, "center"));
    EXPECT_FALSE(cs.Has(Sid("justify-content")));
}

TEST(StyleMatchTests, CompoundMatch)
{
    const Stylesheet s = ParseStylesheet(".a.b { align-items: center }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle both = t.Add(0, {Sid("a"), Sid("b")});
    const NodeHandle onlyA = t.Add(0, {Sid("a")});
    EXPECT_TRUE(AlignIs(Cascade(both, t.Ops(), idx), "center"));
    EXPECT_FALSE(Cascade(onlyA, t.Ops(), idx).Has(Sid("align-items")));
}

TEST(StyleMatchTests, DescendantAndChild)
{
    {
        const Stylesheet s = ParseStylesheet(".a .b { align-items: center }");
        const StyleIndex idx = BuildIndex(s);
        Tree t;
        const NodeHandle a = t.Add(0, {Sid("a")});
        const NodeHandle mid = t.Add(a, {});
        const NodeHandle b = t.Add(mid, {Sid("b")});
        const NodeHandle lone = t.Add(0, {Sid("b")});
        EXPECT_TRUE(AlignIs(Cascade(b, t.Ops(), idx), "center"));
        EXPECT_FALSE(Cascade(lone, t.Ops(), idx).Has(Sid("align-items")));
    }
    {
        const Stylesheet s = ParseStylesheet(".a > .b { align-items: center }");
        const StyleIndex idx = BuildIndex(s);
        Tree t;
        const NodeHandle a = t.Add(0, {Sid("a")});
        const NodeHandle directB = t.Add(a, {Sid("b")});
        const NodeHandle mid = t.Add(a, {});
        const NodeHandle deepB = t.Add(mid, {Sid("b")});
        EXPECT_TRUE(AlignIs(Cascade(directB, t.Ops(), idx), "center"));
        EXPECT_FALSE(Cascade(deepB, t.Ops(), idx).Has(Sid("align-items")));
    }
}

TEST(StyleMatchTests, SpecificityWins)
{
    const Stylesheet s = ParseStylesheet(".a { align-items: center } .a.b { align-items: stretch }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle n = t.Add(0, {Sid("a"), Sid("b")});
    EXPECT_TRUE(AlignIs(Cascade(n, t.Ops(), idx), "stretch"));
}

TEST(StyleMatchTests, SourceOrderTie)
{
    const Stylesheet s = ParseStylesheet(".a { align-items: center } .a { align-items: stretch }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle n = t.Add(0, {Sid("a")});
    EXPECT_TRUE(AlignIs(Cascade(n, t.Ops(), idx), "stretch"));
}

TEST(StyleMatchTests, ShorthandDropsOutrankedLonghand)
{
    const Stylesheet s = ParseStylesheet(".a { margin-left: 4px; padding: 0; padding-top: 2px }"
        ".a.b { margin: 0 }"
        ".c { margin-top: 1px; margin: 0 }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const ComputedStyle a = Cascade(t.Add(0, {Sid("a")}), t.Ops(), idx);
    EXPECT_TRUE(a.Has(Sid("margin-left")));
    EXPECT_TRUE(a.Has(Sid("padding-top")));
    EXPECT_FALSE(Cascade(t.Add(0, {Sid("a"), Sid("b")}), t.Ops(), idx).Has(Sid("margin-left")));
    EXPECT_FALSE(Cascade(t.Add(0, {Sid("c")}), t.Ops(), idx).Has(Sid("margin-top")));
}

TEST(StyleMatchTests, ImportantBeatsSpecificity)
{
    const Stylesheet s = ParseStylesheet(".a { align-items: center !important } .a.b { align-items: stretch }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle n = t.Add(0, {Sid("a"), Sid("b")});
    EXPECT_TRUE(AlignIs(Cascade(n, t.Ops(), idx), "center"));
}

TEST(StyleMatchTests, ChildAfterDescendantBacktracks)
{
    const Stylesheet s = ParseStylesheet(".a > .b .c { align-items: center }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle a = t.Add(0, {Sid("a")});
    const NodeHandle b2 = t.Add(a, {Sid("b")});
    const NodeHandle b1 = t.Add(b2, {Sid("b")});
    const NodeHandle c = t.Add(b1, {Sid("c")});
    EXPECT_TRUE(AlignIs(Cascade(c, t.Ops(), idx), "center"));

    Tree t2;
    const NodeHandle x = t2.Add(0, {});
    const NodeHandle y2 = t2.Add(x, {Sid("b")});
    const NodeHandle y1 = t2.Add(y2, {Sid("b")});
    const NodeHandle z = t2.Add(y1, {Sid("c")});
    EXPECT_FALSE(Cascade(z, t2.Ops(), idx).Has(Sid("align-items")));
}

TEST(StyleMatchTests, PseudoMatch)
{
    const Stylesheet s = ParseStylesheet(".btn:hover { align-items: center }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle hovered = t.Add(0, {Sid("btn")}, static_cast<uint8_t>(Pseudo::Hover));
    const NodeHandle idle = t.Add(0, {Sid("btn")});
    EXPECT_TRUE(AlignIs(Cascade(hovered, t.Ops(), idx), "center"));
    EXPECT_FALSE(Cascade(idle, t.Ops(), idx).Has(Sid("align-items")));
}

TEST(StyleMatchTests, RootDescendant)
{
    const Stylesheet s = ParseStylesheet(":root .a { align-items: center }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle root = t.Add(0, {}, 0, true);
    const NodeHandle a = t.Add(root, {Sid("a")});
    EXPECT_TRUE(AlignIs(Cascade(a, t.Ops(), idx), "center"));

    const NodeHandle nonRoot = t.Add(0, {}, 0, false);
    const NodeHandle a2 = t.Add(nonRoot, {Sid("a")});
    EXPECT_FALSE(Cascade(a2, t.Ops(), idx).Has(Sid("align-items")));
}

TEST(StyleMatchTests, UniversalMatch)
{
    const Stylesheet s = ParseStylesheet("* { align-items: center }");
    const StyleIndex idx = BuildIndex(s);
    Tree t;
    const NodeHandle n = t.Add(0, {});
    EXPECT_TRUE(AlignIs(Cascade(n, t.Ops(), idx), "center"));
}
