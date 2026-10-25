#include <gtest/gtest.h>

#include <lumora/Text/FontSystem.h>

#include <fstream>
#include <iterator>
#include <vector>

using namespace lumora;

namespace
{
    std::vector<CachedGlyph> Glyphs(const FontSystem& fs, std::string_view text)
    {
        std::vector<CachedGlyph> cov, col;
        fs.BuildGlyphCache(text, {}, 13.f, {}, AvailableSpace::MaxContent(), cov, col);
        return cov;
    }
}

// Each paragraph after a '\n' gets its own bidi levels; reading past the first paragraph reordered later lines.
TEST(TextLayout, LinesAfterNewlineKeepLogicalOrder)
{
    std::ifstream f(LUMORA_TEST_FONT, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), {});
    ASSERT_FALSE(raw.empty());
    FontSystem fs;
    fs.RegisterFont({reinterpret_cast<const std::byte*>(raw.data()), raw.size()}, {.family = "Sans"});

    const std::vector<CachedGlyph> alone = Glyphs(fs, "Multi-line, with undo, selection");
    const std::vector<CachedGlyph> after = Glyphs(fs, "Lumora text editor.\nMulti-line, with undo, selection");
    ASSERT_GE(after.size(), alone.size());

    const std::size_t base = after.size() - alone.size();
    for (std::size_t i = 0; i < alone.size(); ++i)
    {
        const CachedGlyph& a = alone[i];
        const CachedGlyph& b = after[base + i];
        EXPECT_EQ(a.sx, b.sx) << "glyph " << i;
        EXPECT_EQ(a.sy, b.sy) << "glyph " << i;
        EXPECT_NEAR(a.local.x - alone[0].local.x, b.local.x - after[base].local.x, 0.01f) << "glyph " << i;
    }
}

TEST(TextLayout, NewlinesEmitNoGlyph)
{
    std::ifstream f(LUMORA_TEST_FONT, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), {});
    ASSERT_FALSE(raw.empty());
    FontSystem fs;
    fs.RegisterFont({reinterpret_cast<const std::byte*>(raw.data()), raw.size()}, {.family = "Sans"});

    EXPECT_EQ(Glyphs(fs, "ab\ncd\r\nef").size(), Glyphs(fs, "abcdef").size());
}
