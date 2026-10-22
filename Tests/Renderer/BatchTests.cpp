#include <gtest/gtest.h>
#include "BatchBuilder.h"

#include <array>
#include <vector>

using namespace lumora;

namespace {

[[nodiscard]] RectCmd Fill(Rect bounds, Color c) {
    return RectCmd{.bounds = bounds, .fill = c};
}

} // namespace

TEST(BatchTests, RectMakesOneQuadBatch) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.AddRect(Fill({0.f, 0.f, 10.f, 10.f}, Color::Red));
    b.End();

    EXPECT_EQ(b.Vertices().size(), 4u);
    EXPECT_EQ(b.Batches().size(), 1u);
    EXPECT_EQ(b.Batches()[0].pipeline, PipelineKind::Rect);
    EXPECT_EQ(b.Batches()[0].textureKey, 0u);
    EXPECT_EQ(b.Batches()[0].firstVertex, 0u);
    EXPECT_EQ(b.Batches()[0].vertexCount, 4u);
    EXPECT_EQ(b.Batches()[0].indexCount, 6u);
}

TEST(BatchTests, RectsEachGetOwnBatch) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.AddRect(Fill({0.f, 0.f, 10.f, 10.f}, Color::Red));
    b.AddRect(Fill({20.f, 0.f, 10.f, 10.f}, Color::Green));
    b.End();

    EXPECT_EQ(b.Batches().size(), 2u);
    EXPECT_EQ(b.Batches()[0].firstVertex, 0u);
    EXPECT_EQ(b.Batches()[0].vertexCount, 4u);
    EXPECT_EQ(b.Batches()[0].indexCount, 6u);
    EXPECT_EQ(b.Batches()[1].firstVertex, 4u);
    EXPECT_EQ(b.Batches()[1].vertexCount, 4u);
    EXPECT_EQ(b.Batches()[1].indexCount, 6u);
}

TEST(BatchTests, RectRecordsSdfParams) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.AddRect(RectCmd{.bounds      = {0.f, 0.f, 40.f, 20.f},
                      .fill        = Color::Red,
                      .radius      = 6.f,
                      .borderColor = Color::Blue,
                      .borderWidth = 2.f});
    b.End();

    EXPECT_EQ(b.Batches().size(), 1u);
    const Batch& batch = b.Batches()[0];
    EXPECT_EQ(batch.pipeline, PipelineKind::Rect);
    EXPECT_EQ(batch.rectHalf, (Vec2{20.f, 10.f}));
    EXPECT_FLOAT_EQ(batch.rectRadius, 6.f);
    EXPECT_FLOAT_EQ(batch.rectBorderWidth, 2.f);
    EXPECT_EQ(b.Vertices()[0].uv, (Vec2{-20.f, -10.f}));
    EXPECT_EQ(b.Vertices()[2].uv, (Vec2{20.f, 10.f}));
}

TEST(BatchTests, PipelineKindBreaksBatch) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.AddRect(Fill({0.f, 0.f, 10.f, 10.f}, Color::Red));
    const std::array<GlyphQuad, 1> quads{GlyphQuad{Rect{0.f, 0.f, 8.f, 8.f}, Rect{0.f, 0.f, 1.f, 1.f}}};
    b.AddGlyphs(quads, 5, Color::Black);
    b.End();

    EXPECT_EQ(b.Batches().size(), 2u);
    EXPECT_EQ(b.Batches()[0].pipeline, PipelineKind::Rect);
    EXPECT_EQ(b.Batches()[1].pipeline, PipelineKind::Textured);
    EXPECT_EQ(b.Batches()[1].textureKey, 5u);
}

TEST(BatchTests, TextureKeyControlsMerge) {
    const std::array<GlyphQuad, 1> q{GlyphQuad{Rect{0.f, 0.f, 8.f, 8.f}, Rect{0.f, 0.f, 1.f, 1.f}}};
    {
        BatchBuilder b;
        b.Begin({100.f, 100.f});
        b.AddGlyphs(q, 3, Color::Black);
        b.AddGlyphs(q, 3, Color::Black);
        b.End();
        EXPECT_EQ(b.Batches().size(), 1u);
    }
    {
        BatchBuilder b;
        b.Begin({100.f, 100.f});
        b.AddGlyphs(q, 3, Color::Black);
        b.AddGlyphs(q, 4, Color::Black);
        b.End();
        EXPECT_EQ(b.Batches().size(), 2u);
    }
}

TEST(BatchTests, ClipSetsScissor) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.PushClip({10.f, 10.f, 30.f, 30.f});
    b.AddRect(Fill({0.f, 0.f, 100.f, 100.f}, Color::Red));
    b.PopClip();
    b.End();

    EXPECT_EQ(b.Batches().size(), 1u);
    const ScissorRect s = b.Batches()[0].scissor;
    EXPECT_EQ(s.x, 10);
    EXPECT_EQ(s.y, 10);
    EXPECT_EQ(s.w, 30);
    EXPECT_EQ(s.h, 30);
}

TEST(BatchTests, NestedClipsIntersect) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.PushClip({10.f, 10.f, 80.f, 80.f});
    b.PushClip({0.f, 0.f, 30.f, 30.f});
    b.AddRect(Fill({0.f, 0.f, 100.f, 100.f}, Color::Red));
    b.PopClip();
    b.PopClip();
    b.End();

    const ScissorRect s = b.Batches()[0].scissor;
    EXPECT_EQ(s.x, 10);
    EXPECT_EQ(s.y, 10);
    EXPECT_EQ(s.w, 20);
    EXPECT_EQ(s.h, 20);
}

TEST(BatchTests, LayerTranslateFoldsIntoVertices) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    const float translate[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, -10.f, -20.f, 1.f};
    b.PushLayer(1.f, translate);
    b.AddRect(Fill({0.f, 0.f, 5.f, 5.f}, Color::Red));
    b.PopLayer();
    b.End();

    EXPECT_EQ(b.Vertices().size(), 4u);
    EXPECT_EQ(b.Vertices()[0].pos, (Vec2{-10.f, -20.f}));
    EXPECT_EQ(b.Vertices()[2].pos, (Vec2{-5.f, -15.f}));
    EXPECT_FALSE(b.SawSkewedClip());
}

TEST(BatchTests, TrianglesCopyVerbatim) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    const std::array<PaintVertex, 3> tri{PaintVertex{{0.f, 0.f}, Color::Red},
                                         PaintVertex{{10.f, 0.f}, Color::Green},
                                         PaintVertex{{0.f, 10.f}, Color::Blue}};
    b.AddTriangles(tri);
    b.End();

    EXPECT_EQ(b.Vertices().size(), 3u);
    EXPECT_EQ(b.Batches().size(), 1u);
    EXPECT_EQ(b.Batches()[0].pipeline, PipelineKind::Tris);
    EXPECT_EQ(b.Batches()[0].firstVertex, 0u);
    EXPECT_EQ(b.Batches()[0].vertexCount, 3u);
}

TEST(BatchTests, RoundedClipMakesRrect) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.PushClip({10.f, 20.f, 40.f, 60.f}, 8.f);
    b.AddRect(Fill({0.f, 0.f, 100.f, 100.f}, Color::Red));
    b.PopClip();
    b.End();

    const ClipRef& c = b.Batches()[0].clip;
    EXPECT_EQ(c.mode, ClipMode::Rrect);
    EXPECT_EQ(c.center, (Vec2{30.f, 50.f}));
    EXPECT_EQ(c.halfExtent, (Vec2{20.f, 30.f}));
    EXPECT_FLOAT_EQ(c.radius, 8.f);
    EXPECT_FLOAT_EQ(c.rot.x, 1.f);
    EXPECT_FLOAT_EQ(c.rot.y, 0.f);
}

TEST(BatchTests, SquareClipStaysScissorOnly) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.PushClip({10.f, 10.f, 30.f, 30.f}, 0.f);
    b.AddRect(Fill({0.f, 0.f, 100.f, 100.f}, Color::Red));
    b.PopClip();
    b.End();
    EXPECT_EQ(b.Batches()[0].clip.mode, ClipMode::None);
    const ScissorRect s = b.Batches()[0].scissor;
    EXPECT_EQ(s.x, 10);
    EXPECT_EQ(s.y, 10);
    EXPECT_EQ(s.w, 30);
    EXPECT_EQ(s.h, 30);
}

TEST(BatchTests, SquareChildInheritsRoundedParent) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.PushClip({0.f, 0.f, 80.f, 80.f}, 12.f);
    b.PushClip({10.f, 10.f, 30.f, 30.f});
    b.AddRect(Fill({0.f, 0.f, 100.f, 100.f}, Color::Red));
    b.PopClip();
    b.PopClip();
    b.End();
    const ClipRef& c = b.Batches()[0].clip;
    EXPECT_EQ(c.mode, ClipMode::Rrect);
    EXPECT_FLOAT_EQ(c.radius, 12.f);
    const ScissorRect s = b.Batches()[0].scissor;
    EXPECT_EQ(s.x, 10);
    EXPECT_EQ(s.y, 10);
    EXPECT_EQ(s.w, 30);
    EXPECT_EQ(s.h, 30);
}

TEST(BatchTests, ClipBreaksAndMergesBatches) {
    const std::array<GlyphQuad, 1> q{GlyphQuad{Rect{0.f, 0.f, 8.f, 8.f}, Rect{0.f, 0.f, 1.f, 1.f}}};
    {
        BatchBuilder b;
        b.Begin({100.f, 100.f});
        b.PushClip({0.f, 0.f, 50.f, 50.f}, 6.f);
        b.AddGlyphs(q, 3, Color::Black);
        b.AddGlyphs(q, 3, Color::Black);
        b.PopClip();
        b.End();
        EXPECT_EQ(b.Batches().size(), 1u);
    }
    {
        BatchBuilder b;
        b.Begin({100.f, 100.f});
        b.PushClip({0.f, 0.f, 50.f, 50.f}, 6.f);
        b.AddGlyphs(q, 3, Color::Black);
        b.PopClip();
        b.PushClip({0.f, 0.f, 50.f, 50.f}, 10.f);
        b.AddGlyphs(q, 3, Color::Black);
        b.PopClip();
        b.End();
        EXPECT_EQ(b.Batches().size(), 2u);
    }
}

TEST(BatchTests, ClipResolvesThroughLayerTranslate) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    const float translate[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -20.f, 1.f};
    b.PushLayer(1.f, translate);
    b.PushClip({10.f, 30.f, 30.f, 30.f});
    b.AddRect(Fill({0.f, 0.f, 100.f, 100.f}, Color::Red));
    b.PopClip();
    b.PopLayer();
    b.End();

    const ScissorRect s = b.Batches()[0].scissor;
    EXPECT_EQ(s.x, 10);
    EXPECT_EQ(s.y, 10);
    EXPECT_EQ(s.w, 30);
    EXPECT_EQ(s.h, 30);
}

TEST(BatchTests, RoundedClipCentreFollowsLayerTranslate) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    const float translate[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -20.f, 1.f};
    b.PushLayer(1.f, translate);
    b.PushClip({10.f, 30.f, 40.f, 20.f}, 6.f);
    b.AddRect(Fill({0.f, 0.f, 100.f, 100.f}, Color::Red));
    b.PopClip();
    b.PopLayer();
    b.End();

    const ClipRef& c = b.Batches()[0].clip;
    EXPECT_EQ(c.mode, ClipMode::Rrect);
    EXPECT_EQ(c.center, (Vec2{30.f, 20.f}));
}

TEST(BatchTests, RebuildIsDeterministic) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.AddRect(Fill({0.f, 0.f, 10.f, 10.f}, Color::Red));
    b.End();
    const std::size_t v = b.Vertices().size();

    b.Begin({100.f, 100.f});
    b.AddRect(Fill({0.f, 0.f, 10.f, 10.f}, Color::Red));
    b.End();
    EXPECT_EQ(b.Vertices().size(), v);
}

TEST(BatchTests, StaticUploadGeneration) {
    BatchBuilder b;
    b.Begin({100.f, 100.f});
    b.AddRect(Fill({0.f, 0.f, 10.f, 10.f}, Color::Red));
    b.End();
    const std::uint64_t gen1 = b.Generation();

    b.Retain();
    b.Begin({100.f, 100.f});
    EXPECT_NE(b.Generation(), gen1);
    const bool restored = b.Restore();
    EXPECT_TRUE(restored);
    EXPECT_EQ(b.Generation(), gen1);

    b.Begin({100.f, 100.f});
    b.AddRect(Fill({0.f, 0.f, 10.f, 10.f}, Color::Red));
    b.End();
    EXPECT_NE(b.Generation(), gen1);
}
