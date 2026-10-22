#include "Pulse.h"

#include <algorithm>

namespace pulse {

// Full-window, pointer-transparent layer for alarm bursts (expanding rings + confetti).
Elem ParticleOverlay(FeedPtr f)
{
    return Box({.layout = {.width = Expand, .height = Expand},
                .position = {.position = Position::Absolute, .inset = {0.f, 0.f, 0.f, 0.f}, .z = 100},
                .hitThroughSubtree = true},
        Painter({.onPaint = [f](Paint2D& p) {
                     ProfScope prof(Prof::Particles);
                     for (const Particle& q : f->particles) {
                         if (q.life < 0.f)
                             continue;
                         const float t = q.life / q.maxLife;
                         const auto alpha = static_cast<std::uint8_t>(std::clamp(1.f - t, 0.f, 1.f) * 255.f);
                         if (q.ring)
                             p.StrokeCircle(q.pos, q.size + t * 90.f, q.color.WithAlpha(alpha), 3.f * (1.f - t) + 0.5f);
                         else
                             p.FillRect({q.pos.x - q.size, q.pos.y - q.size, q.size * 2.f, q.size * 2.f}, q.color.WithAlpha(alpha), 1.f);
                     }
                 },
                 .repaint = f->frame}));
}

} // namespace pulse
