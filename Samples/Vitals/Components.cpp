#include "Vitals.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace vitals {

namespace {

struct Ripple {
    Vec2   at{};
    double t0 = -10.0;
    bool   held = false;
    Rect   surf{};
};

// Expanding ink circle from the press point, clipped to the card's rounded rect.
Elem RippleLayer(AppPtr m, std::shared_ptr<Ripple> r, float radius)
{
    return Box({.layout = {.width = Expand, .height = Expand},
                .position = {.position = Position::Absolute, .inset = {0.f, 0.f, 0.f, 0.f}},
                .hitThroughSubtree = true},
        Painter({.onPaint = [m, r, radius](Paint2D& p) {
                     r->surf = p.SurfaceBounds();
                     const float age = static_cast<float>(m->time - r->t0);
                     if (age > 0.7f && !r->held)
                         return;
                     const Rect b = p.Bounds();
                     const Vec2 c{r->at.x - r->surf.x + b.x, r->at.y - r->surf.y + b.y};
                     const float maxR = std::hypot(std::max(c.x - b.x, b.x + b.width - c.x), std::max(c.y - b.y, b.y + b.height - c.y));
                     const float grow = 1.f - std::pow(1.f - std::min(age / 0.45f, 1.f), 3.f);
                     const float fade = r->held ? 1.f : std::clamp(1.f - (age - 0.3f) / 0.4f, 0.f, 1.f);
                     p.PushClip(b, radius);
                     p.FillCircle(c, 12.f + (maxR - 12.f) * grow, m->pal.onSurface.WithAlpha(static_cast<std::uint8_t>(30.f * fade)));
                     p.PopClip();
                 },
                 .repaint = m->frame}));
}

} // namespace

Elem Card(AppPtr m, std::string cls, std::function<void()> onTap, Children children)
{
    auto r = std::make_shared<Ripple>();
    InteractProps on;
    on.onPress = [m, r](PointerInfo info) {
        r->at = info.position;
        r->t0 = m->time;
        r->held = true;
    };
    on.onRelease = [r](PointerInfo) { r->held = false; };
    on.onLeave = [r] { r->held = false; };
    on.onClick = std::move(onTap);
    const float radius = cls.find("tile") != std::string::npos ? 20.f : 24.f;
    return Column({.anchor = true, .on = std::move(on), .className = "card " + cls},
        Column({.className = "card-in"}, std::move(children)),
        RippleLayer(m, r, radius));
}

Elem CardHeader(AppPtr m, Ico icon, std::function<Color(const Palette&)> tint, Str title, Str trailing)
{
    return Row({.className = "card-head"},
        IconView(m, icon, 20.f, tint),
        Text(std::move(title), {.className = "card-title"}),
        Spacer(),
        Text(std::move(trailing), {.className = "card-trailing"}));
}

Elem ProgressBar(Reactive<float> fraction, std::string cls)
{
    const Motion<float> shown = Spring<float>(std::move(fraction), 0.f, {.stiffness = 70.f, .damping = 16.f});
    return Row({.className = "bar-track " + cls},
        Box({.layout = {.width = [shown] { return Percent(std::clamp(shown() * 100.f, 3.f, 100.f)); }, .height = Expand},
             .className = "bar-fill"}));
}

Elem TopBar(AppPtr m)
{
    static const char* const kTitles[] = {"Today", "Activity", "Sleep", "Profile"};
    static const char* const kSubs[] = {"Thursday, October 22", "This week", "Last night", "Sam Rivera"};
    const Signal<int> tab = m->tab;
    return Row({.className = "topbar"},
        Column({.className = "topbar-text"},
            Text(Str([tab] { return std::string(kSubs[tab()]); }), {.className = "topbar-sub"}),
            Text(Str([tab] { return std::string(kTitles[tab()]); }), {.className = "topbar-title"})),
        Spacer(),
        Box({.on = {.onClick = [m] { m->Snack("Synced with your watch just now"); }}, .className = "icon-btn"},
            IconView(m, Ico::More, 22.f, [](const Palette& p) { return p.onSurface; })),
        Box({.on = {.onClick = [m] { m->SetTab(3); }}, .className = "avatar small"},
            Text("SR", {.className = "avatar-text"})));
}

Elem BottomNav(AppPtr m)
{
    struct Item { Ico icon; const char* label; };
    static constexpr Item kItems[] = {{Ico::Today, "Today"}, {Ico::Activity, "Activity"}, {Ico::Sleep, "Sleep"}, {Ico::Profile, "Profile"}};
    const Signal<int> tab = m->tab;
    const Motion<float> pos = Spring<float>([tab] { return static_cast<float>(tab()); }, {.stiffness = 260.f, .damping = 24.f});

    std::vector<Elem> items;
    for (int i = 0; i < 4; ++i) {
        items.push_back(Column({.layout = {.width = Expand}, .on = {.onClick = [m, i] { m->SetTab(i); }}, .className = "nav-item"},
            IconView(m, kItems[i].icon, 22.f, [tab, i](const Palette& p) { return tab.Peek() == i ? p.onPrimaryContainer : p.muted; }),
            Text(kItems[i].label, {.className = [tab, i] { return std::string(tab() == i ? "nav-label active" : "nav-label"); }})));
    }

    // The indicator pill slides between items and stretches while moving.
    Elem pill = Box({.layout = {.width = Expand, .height = Expand},
                     .position = {.position = Position::Absolute, .inset = {0.f, 0.f, 0.f, 0.f}},
                     .hitThroughSubtree = true},
        Painter({.onPaint = [m, pos, tab](Paint2D& p) {
                     const Rect b = p.Bounds();
                     const float slot = b.width / 4.f;
                     const float t = pos.Peek();
                     const float stretch = std::min(std::abs(t - static_cast<float>(tab.Peek())), 1.f) * 28.f;
                     const float w = 64.f + stretch;
                     const float cx = b.x + slot * (t + 0.5f);
                     p.FillRect({cx - w * 0.5f, b.y + 12.f, w, 32.f}, m->pal.primaryContainer, 16.f);
                 },
                 .repaint = m->frame}));

    return Row({.anchor = true, .className = "nav"}, std::move(pill), std::move(items));
}

Elem Fab(AppPtr m)
{
    const Signal<bool> snack = m->snackShown;
    const Motion<float> lift = Spring<float>([snack] { return snack() ? -64.f : 0.f; }, {.stiffness = 220.f, .damping = 24.f});
    const Signal<bool> open = m->sheetOpen;
    const Motion<float> fade = Spring<float>([open] { return open() ? 0.f : 1.f; });
    // The outer box owns the inline lift so the `.fab:active` CSS scale on the inner row cannot overwrite it.
    return Box({.style = {.opacity = fade},
                .transform = {.y = lift},
                .position = {.position = Position::Absolute, .inset = {Inset{}.left, Inset{}.top, 18.f, 102.f}, .z = 20}},
        Row({.on = {.onClick = [m] { m->sheetOpen = true; }}, .className = "fab"},
            IconView(m, Ico::Plus, 22.f, [](const Palette& p) { return p.onPrimaryContainer; }),
            Text("Log", {.className = "fab-label"})));
}

Elem Snackbar(AppPtr m)
{
    const Signal<bool> shown = m->snackShown;
    const Motion<float> y = Spring<float>([shown] { return shown() ? 0.f : 120.f; }, {.stiffness = 240.f, .damping = 26.f});
    const Motion<float> op = Spring<float>([shown] { return shown() ? 1.f : 0.f; }, {.stiffness = 240.f, .damping = 26.f});
    return Row({.layout = {.width = Expand},
                .style = {.opacity = op},
                .view = {.visible = [op] { return op() > 0.01f; }},
                .transform = {.y = y},
                .position = {.position = Position::Absolute, .inset = {16.f, Inset{}.top, 16.f, 100.f}, .z = 30},
                .className = "snackbar"},
        Text(m->snackText, {.className = "snack-text"}),
        Spacer(),
        Show([shown] { return shown(); }, [m] {
            return Box({.on = {.onClick = [m] {
                            if (m->undo) {
                                m->undo();
                                m->undo = {};
                                m->Snack("Entry removed");
                            }
                        }},
                        .className = "snack-action"},
                Text("Undo", {.className = "snack-action-text"}));
        }));
}

Elem BottomSheet(AppPtr m)
{
    const Signal<bool> open = m->sheetOpen;
    const Motion<float> y = Spring<float>([open] { return open() ? 0.f : 520.f; }, {.stiffness = 210.f, .damping = 25.f});
    const Motion<float> dim = Spring<float>([open] { return open() ? 1.f : 0.f; }, {.stiffness = 210.f, .damping = 25.f});

    struct Option { Ico icon; const char* title; const char* sub; int kind; };
    static constexpr Option kOptions[] = {
        {Ico::Water, "Water", "+250 ml glass", 0},
        {Ico::Scale, "Weight", "Today's weigh-in", 1},
        {Ico::Mood, "Mood", "How do you feel?", 2},
        {Ico::Run, "Workout", "15 min brisk walk", 3},
    };
    auto act = [m](int kind) {
        switch (kind) {
        case 0: {
            m->water = m->water.Peek() + 1;
            m->Snack(std::format("Added 250 ml water ({} ml today)", m->water.Peek() * 250), [m] { m->water = std::max(0, m->water.Peek() - 1); });
            break;
        }
        case 1: {
            const float prev = m->weight.Peek();
            m->weight = std::round((prev - 0.2f) * 10.f) / 10.f;
            m->Snack("Weight logged: " + WeightText(*m, m->weight.Peek()), [m, prev] { m->weight = prev; });
            break;
        }
        case 2: {
            const int prev = m->mood.Peek();
            m->mood = prev % 5 + 1;
            static const char* const kMoods[] = {"", "Rough", "Low", "Okay", "Good", "Great"};
            m->Snack(std::format("Mood logged: {}", kMoods[m->mood.Peek()]), [m, prev] { m->mood = prev; });
            break;
        }
        default: {
            const int id = m->nextWorkoutId++;
            m->workouts.Update([id](std::vector<Workout>& w) {
                w.insert(w.begin(), Workout{id, Ico::Run, "Brisk walk", "Just now", 15, 74, 1.4f});
            });
            m->exerciseMin = m->exerciseMin.Peek() + 15.f;
            m->Snack("Workout saved: 15 min walk", [m, id] {
                m->workouts.Update([id](std::vector<Workout>& w) { std::erase_if(w, [id](const Workout& x) { return x.id == id; }); });
                m->exerciseMin = m->exerciseMin.Peek() - 15.f;
            });
            break;
        }
        }
        m->sheetCloseAt = m->time + 0.22; // let the ripple read before the sheet drops
    };

    std::vector<Elem> tiles;
    for (const Option& o : kOptions) {
        const int kind = o.kind;
        tiles.push_back(Card(m, "tile", [act, kind] { act(kind); }, {
            Box({.className = "tile-icon"}, IconView(m, o.icon, 24.f, [](const Palette& p) { return p.onPrimaryContainer; })),
            Text(o.title, {.className = "tile-title"}),
            Text(o.sub, {.className = "tile-sub"}),
        }));
    }

    Elem scrim = Box({.layout = {.width = Expand, .height = Expand},
                      .style = {.opacity = dim},
                      .view = {.visible = [dim] { return dim() > 0.01f; }},
                      .position = {.position = Position::Absolute, .inset = {0.f, 0.f, 0.f, 0.f}, .z = 40},
                      .on = {.onClick = [m] { m->sheetOpen = false; }},
                      .className = "scrim"});

    Elem sheet = Column({.layout = {.width = Expand},
                         .view = {.visible = [y] { return y() < 519.f; }},
                         .transform = {.y = y},
                         .position = {.position = Position::Absolute, .inset = {0.f, Inset{}.top, 0.f, 0.f}, .z = 50},
                         .on = {.onClick = [] {}}, // swallow taps so they never reach the scrim
                         .className = "sheet"},
        Box({.className = "sheet-handle"}),
        Text("Quick log", {.className = "sheet-title"}),
        Text("Tap to record. Entries sync to your history.", {.className = "sheet-sub"}),
        Grid({.columns = 2, .rows = 2, .gap = 12.f, .layout = {.width = Expand, .height = Fixed(268.f)}}, std::move(tiles)),
        Box({.on = {.onClick = [m] { m->sheetOpen = false; }}, .className = "sheet-close"},
            Text("Cancel", {.className = "sheet-close-text"})));

    return Box({.layout = {.width = Expand, .height = Expand},
                .position = {.position = Position::Absolute, .inset = {0.f, 0.f, 0.f, 0.f}, .z = 40},
                .anchor = true,
                .on = {.hitThrough = true}},
        std::move(scrim), std::move(sheet));
}

// Three concentric rings springing from zero to their fraction of goal.
Elem RingChart(AppPtr m, float size)
{
    const Signal<float> steps = m->steps, ex = m->exerciseMin, stand = m->standHours;
    const Signal<double> moveGoal = m->moveGoal;
    const Vitals* raw = m.get();
    const SpringConfig sc{.stiffness = 60.f, .damping = 14.f};
    const Motion<float> move = Spring<float>([raw, steps, moveGoal] { return raw->MoveKcal(steps()) / static_cast<float>(moveGoal()); }, 0.f, sc);
    const Motion<float> exercise = Spring<float>([ex] { return ex() / 30.f; }, 0.f, sc);
    const Motion<float> standM = Spring<float>([stand] { return stand() / 12.f; }, 0.f, sc);

    return Painter({.width = Fixed(size), .height = Fixed(size),
                    .onPaint = [m, move, exercise, standM](Paint2D& p) {
                        const Rect b = p.Bounds();
                        const Vec2 c{b.x + b.width * 0.5f, b.y + b.height * 0.5f};
                        const float w = b.width * 0.105f;
                        const float rings[3] = {move.Peek(), exercise.Peek(), standM.Peek()};
                        const Color cols[3] = {m->pal.move, m->pal.exercise, m->pal.stand};
                        for (int i = 0; i < 3; ++i) {
                            const float r = b.width * 0.5f - w * 0.5f - static_cast<float>(i) * (w + 3.f);
                            const float t = std::clamp(rings[i], 0.f, 1.f);
                            p.StrokeCircle(c, r, cols[i].WithAlpha(46), w);
                            if (t <= 0.002f)
                                continue;
                            // Paint2D arcs are y-down: start at 12 o'clock, sweep clockwise.
                            const float a0 = -kPi * 0.5f, a1 = a0 + 2.f * kPi * t;
                            p.StrokeArc(c, r, a0, a1, cols[i], w);
                            p.FillCircle({c.x, c.y - r}, w * 0.5f, cols[i]);
                            const Vec2 tip{c.x + std::cos(a1) * r, c.y + std::sin(a1) * r};
                            p.FillCircle(tip, w * 0.5f, cols[i]);
                        }
                    },
                    .repaint = m->frame});
}

Elem ScoreRing(AppPtr m, float score, float size)
{
    const Motion<float> shown = Spring<float>(score, 0.f, {.stiffness = 50.f, .damping = 13.f});
    return Painter({.width = Fixed(size), .height = Fixed(size),
                    .onPaint = [m, shown](Paint2D& p) {
                        const Rect b = p.Bounds();
                        const Vec2 c{b.x + b.width * 0.5f, b.y + b.height * 0.5f};
                        const float w = 12.f, r = b.width * 0.5f - w * 0.5f - 2.f;
                        const float t = std::clamp(shown.Peek() / 100.f, 0.f, 1.f);
                        p.StrokeCircle(c, r, m->pal.cardVar, w);
                        const float a0 = -kPi * 0.5f, a1 = a0 + 2.f * kPi * t;
                        p.StrokeArc(c, r, a0, a1, m->pal.primary, w);
                        p.FillCircle({c.x, c.y - r}, w * 0.5f, m->pal.primary);
                        p.FillCircle({c.x + std::cos(a1) * r, c.y + std::sin(a1) * r}, w * 0.5f, m->pal.primary);
                        const std::string num = std::format("{:.0f}", shown.Peek());
                        const float fs = b.width * 0.3f;
                        p.DrawString(num, {c.x - static_cast<float>(num.size()) * fs * 0.28f, c.y - fs * 0.72f}, m->pal.onSurface, fs, true);
                        p.DrawString("score", {c.x - 16.f, c.y + fs * 0.36f}, m->pal.muted, 12.f);
                    },
                    .repaint = m->frame});
}

} // namespace vitals
