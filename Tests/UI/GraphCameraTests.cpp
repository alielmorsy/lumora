#include <gtest/gtest.h>
#include <lumora/Graph/Camera.h>

#include <cmath>

using namespace lumora;
using namespace lumora::nodegraph;

namespace
{
    [[nodiscard]] bool Near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }
    [[nodiscard]] bool Near(Vec2 a, Vec2 b, float eps = 0.001f)
    {
        return Near(a.x, b.x, eps) && Near(a.y, b.y, eps);
    }

    constexpr Rect kViewport{10.f, 20.f, 800.f, 600.f};
} // namespace

TEST(GraphCameraTests, IdentityMapsCanvasOriginToTheViewportOrigin)
{
    Camera cam;
    EXPECT_TRUE(Near(cam.Zoom(), 1.f));
    EXPECT_TRUE(Near(cam.ToLayer(Vec2{0.f, 0.f}, kViewport), Vec2{10.f, 20.f}));
    EXPECT_TRUE(Near(cam.ToLayer(Vec2{5.f, 7.f}, kViewport), Vec2{15.f, 27.f}));
}

TEST(GraphCameraTests, ToCanvasIsTheInverseOfToLayer)
{
    Camera cam;
    cam.SetPan({123.f, -45.f});
    cam.SetZoom(2.5f);

    const Vec2 canvas{77.f, 19.f};
    EXPECT_TRUE(Near(cam.ToCanvas(cam.ToLayer(canvas, kViewport), kViewport), canvas));

    const Vec2 layer{300.f, 400.f};
    EXPECT_TRUE(Near(cam.ToLayer(cam.ToCanvas(layer, kViewport), kViewport), layer));
}

TEST(GraphCameraTests, ZoomIsClampedBothWays)
{
    Camera cam;
    cam.SetZoom(100.f);
    EXPECT_TRUE(Near(cam.Zoom(), Camera::MaxZoom));
    cam.SetZoom(0.0001f);
    EXPECT_TRUE(Near(cam.Zoom(), Camera::MinZoom));
    cam.SetZoom(-3.f);
    EXPECT_TRUE(Near(cam.Zoom(), Camera::MinZoom));
}

TEST(GraphCameraTests, ZoomAtKeepsTheAnchoredCanvasPointUnderTheCursor)
{
    Camera cam;
    cam.SetPan({40.f, 60.f});

    const Vec2 anchor{410.f, 320.f};
    const Vec2 before = cam.ToCanvas(anchor, kViewport);
    cam.ZoomAt(anchor, 1.25f, kViewport);
    const Vec2 after = cam.ToCanvas(anchor, kViewport);

    EXPECT_TRUE(Near(cam.Zoom(), 1.25f));
    EXPECT_TRUE(Near(before, after));
}

TEST(GraphCameraTests, ZoomAtStillHoldsTheAnchorWhenTheFactorIsClamped)
{
    Camera cam;
    cam.SetZoom(Camera::MaxZoom);

    const Vec2 anchor{100.f, 100.f};
    const Vec2 before = cam.ToCanvas(anchor, kViewport);
    cam.ZoomAt(anchor, 8.f, kViewport);

    EXPECT_TRUE(Near(cam.Zoom(), Camera::MaxZoom));
    EXPECT_TRUE(Near(before, cam.ToCanvas(anchor, kViewport)));
}

TEST(GraphCameraTests, PanByMovesInLayerPixelsNotCanvasUnits)
{
    Camera cam;
    cam.SetZoom(2.f);
    cam.PanBy({20.f, -40.f});
    EXPECT_TRUE(Near(cam.Pan(), Vec2{-10.f, 20.f}));
}

TEST(GraphCameraTests, VisibleCanvasCoversTheViewportInCanvasUnits)
{
    Camera cam;
    cam.SetPan({5.f, 6.f});
    cam.SetZoom(2.f);

    const Rect vis = cam.VisibleCanvas(kViewport);
    EXPECT_TRUE(Near(vis.x, 5.f));
    EXPECT_TRUE(Near(vis.y, 6.f));
    EXPECT_TRUE(Near(vis.width, 400.f));
    EXPECT_TRUE(Near(vis.height, 300.f));
}

TEST(GraphCameraTests, FitCentresTheBoundsAndLeavesAMargin)
{
    Camera cam;
    const Rect bounds{0.f, 0.f, 360.f, 260.f};
    cam.Fit(bounds, kViewport, 40.f);

    EXPECT_TRUE(Near(cam.Zoom(), 2.f));

    const Rect vis = cam.VisibleCanvas(kViewport);
    EXPECT_TRUE(Near(vis.Center().x, bounds.Center().x));
    EXPECT_TRUE(Near(vis.Center().y, bounds.Center().y));
    EXPECT_GE(vis.width, bounds.width);
    EXPECT_GE(vis.height, bounds.height);
}

TEST(GraphCameraTests, FitIgnoresDegenerateBounds)
{
    Camera cam;
    cam.SetPan({9.f, 9.f});
    cam.SetZoom(1.5f);
    cam.Fit(Rect{}, kViewport, 40.f);
    EXPECT_TRUE(Near(cam.Pan(), Vec2{9.f, 9.f}));
    EXPECT_TRUE(Near(cam.Zoom(), 1.5f));
}

TEST(GraphCameraTests, FitClampsRatherThanExceedingMaxZoom)
{
    Camera cam;
    cam.Fit(Rect{0.f, 0.f, 1.f, 1.f}, kViewport, 40.f);
    EXPECT_TRUE(Near(cam.Zoom(), Camera::MaxZoom));
}
