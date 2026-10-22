#include "Vitals.h"

#include <cmath>

namespace vitals {

namespace {

// Arc points, angles in radians, y-down (clockwise) like Paint2D arcs.
void ArcPts(std::vector<Vec2>& out, Vec2 c, float r, float a0, float a1, int n)
{
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n);
        out.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
}

} // namespace

// Icons are authored on a 24x24 grid and scaled into `box`.
void DrawIcon(Paint2D& p, Ico icon, Rect box, Color c)
{
    const float s = std::min(box.width, box.height) / 24.f;
    const float ox = box.x + (box.width - 24.f * s) * 0.5f, oy = box.y + (box.height - 24.f * s) * 0.5f;
    auto P = [&](float x, float y) { return Vec2{ox + x * s, oy + y * s}; };
    auto R = [&](float x, float y, float w, float h, float r) { p.FillRect({ox + x * s, oy + y * s, w * s, h * s}, c, r * s); };
    auto L = [&](float x0, float y0, float x1, float y1, float w) { p.DrawLine(P(x0, y0), P(x1, y1), c, w * s, LineCap::Round); };
    auto C = [&](float x, float y, float r) { p.FillCircle(P(x, y), r * s, c); };
    std::vector<Vec2> pts;

    switch (icon) {
    case Ico::Today: // concentric rings
        p.StrokeCircle(P(12, 12), 9.f * s, c, 2.2f * s);
        p.StrokeCircle(P(12, 12), 5.f * s, c, 2.2f * s);
        C(12, 12, 1.6f);
        break;
    case Ico::Activity: // bars
        R(3.5f, 12, 4, 9, 1.5f);
        R(10, 6, 4, 15, 1.5f);
        R(16.5f, 9, 4, 12, 1.5f);
        break;
    case Ico::Sleep:
    case Ico::Moon: { // crescent
        ArcPts(pts, P(12, 12), 9.f * s, -kPi * 0.35f, -kPi * 1.85f, 24);
        ArcPts(pts, P(15.5f, 9.5f), 7.f * s, -kPi * 1.62f, -kPi * 0.58f, 24);
        p.FillPolygon(pts, c);
        break;
    }
    case Ico::Profile:
        C(12, 8, 4.2f);
        p.FillPie(P(12, 21), 8.f * s, kPi, 2.f * kPi, c);
        break;
    case Ico::Heart:
        C(8.2f, 9.2f, 4.6f);
        C(15.8f, 9.2f, 4.6f);
        p.FillTriangle(P(3.75f, 10.6f), P(20.25f, 10.6f), P(12, 20.5f), c);
        break;
    case Ico::Steps:
        R(5, 3, 5, 9, 2.5f);
        R(5.5f, 13, 4, 3.4f, 1.6f);
        R(14, 7, 5, 9, 2.5f);
        R(14.5f, 17, 4, 3.4f, 1.6f);
        break;
    case Ico::Water:
        C(12, 14.5f, 6.5f);
        p.FillTriangle(P(12, 2.5f), P(5.9f, 12.4f), P(18.1f, 12.4f), c);
        break;
    case Ico::Flame: // round base with three tongues
        C(12, 15.5f, 6.2f);
        p.FillTriangle(P(6.2f, 14.5f), P(12.5f, 1.5f), P(15.5f, 13.f), c);
        p.FillTriangle(P(5.9f, 15.f), P(6.f, 6.5f), P(10.f, 12.f), c);
        p.FillTriangle(P(18.1f, 15.f), P(17.f, 7.f), P(13.f, 11.f), c);
        break;
    case Ico::Bolt:
        pts = {P(14, 2), P(5, 13.5f), P(11, 13.5f), P(10, 22), P(19, 10.5f), P(13, 10.5f)};
        p.FillPolygon(pts, c);
        break;
    case Ico::More:
        C(12, 5.5f, 1.9f);
        C(12, 12, 1.9f);
        C(12, 18.5f, 1.9f);
        break;
    case Ico::Plus:
        L(12, 5, 12, 19, 2.4f);
        L(5, 12, 19, 12, 2.4f);
        break;
    case Ico::Clock:
        p.StrokeCircle(P(12, 12), 8.5f * s, c, 2.f * s);
        L(12, 12, 12, 7.5f, 2.f);
        L(12, 12, 15.5f, 14, 2.f);
        break;
    case Ico::Scale:
        p.StrokeRect({ox + 3.5f * s, oy + 3.5f * s, 17.f * s, 17.f * s}, c, 2.f * s, 5.f * s);
        p.StrokeArc(P(12, 12.5f), 5.f * s, kPi * 1.25f, kPi * 1.75f, c, 2.f * s);
        L(12, 9, 13.8f, 7.6f, 1.8f);
        break;
    case Ico::Mood:
        p.StrokeCircle(P(12, 12), 8.5f * s, c, 2.f * s);
        C(9, 10, 1.4f);
        C(15, 10, 1.4f);
        p.StrokeArc(P(12, 12), 4.5f * s, kPi * 0.15f, kPi * 0.85f, c, 1.9f * s);
        break;
    case Ico::Run:
        C(14.5f, 4.5f, 2.3f);
        L(13, 8.5f, 10.5f, 14.5f, 2.6f);
        L(10.5f, 14.5f, 14.5f, 17.5f, 2.3f);
        L(14.5f, 17.5f, 13, 21.5f, 2.3f);
        L(10.5f, 14.5f, 6.5f, 19.5f, 2.3f);
        L(12.5f, 9, 17.5f, 12, 2.1f);
        L(12.5f, 9, 8, 10.5f, 2.1f);
        break;
    case Ico::Bike:
        p.StrokeCircle(P(6, 16), 4.f * s, c, 2.f * s);
        p.StrokeCircle(P(18, 16), 4.f * s, c, 2.f * s);
        L(6, 16, 10.5f, 8.5f, 2.f);
        L(10.5f, 8.5f, 15.5f, 8.5f, 2.f);
        L(15.5f, 8.5f, 18, 16, 2.f);
        L(10.5f, 8.5f, 12, 16, 2.f);
        L(9, 6, 12, 6, 2.f);
        break;
    case Ico::Swim:
        C(16.5f, 6, 2.3f);
        for (float y : {14.f, 19.f}) {
            pts.clear();
            for (int i = 0; i <= 16; ++i) {
                const float x = 3.f + 18.f * static_cast<float>(i) / 16.f;
                pts.push_back(P(x, y + std::sin(static_cast<float>(i) / 16.f * kPi * 3.f) * 1.3f));
            }
            p.DrawPolyline(pts, c, 2.f * s, false, LineJoin::Round, LineCap::Round);
        }
        L(6, 11, 12, 8, 2.2f);
        break;
    case Ico::Lift:
        R(2.5f, 9, 3, 6, 1);
        R(5.5f, 6.5f, 3, 11, 1.2f);
        R(15.5f, 6.5f, 3, 11, 1.2f);
        R(18.5f, 9, 3, 6, 1);
        R(8.5f, 11, 7, 2, 0.5f);
        break;
    case Ico::Pin:
        C(12, 9.5f, 6.5f);
        p.FillTriangle(P(6.6f, 13), P(17.4f, 13), P(12, 21.5f), c);
        break;
    case Ico::Stairs:
        pts = {P(3, 20), P(3, 16), P(8, 16), P(8, 11), P(13, 11), P(13, 6), P(19, 6), P(19, 4), P(21, 4), P(21, 20)};
        p.FillPolygon(pts, c);
        break;
    case Ico::Sun:
        C(12, 12, 4.5f);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * kPi / 4.f;
            L(12 + std::cos(a) * 7.2f, 12 + std::sin(a) * 7.2f, 12 + std::cos(a) * 9.6f, 12 + std::sin(a) * 9.6f, 2.f);
        }
        break;
    case Ico::Check:
        pts = {P(5, 12.5f), P(10, 17.5f), P(19, 7)};
        p.DrawPolyline(pts, c, 2.6f * s, false, LineJoin::Round, LineCap::Round);
        break;
    }
}

Elem IconView(AppPtr m, Ico icon, float size, std::function<Color(const Palette&)> color)
{
    return Painter({.width = Fixed(size), .height = Fixed(size),
                    .onPaint = [m, icon, color](Paint2D& p) { DrawIcon(p, icon, p.Bounds(), color(m->pal)); },
                    .repaint = m->frame});
}

} // namespace vitals
