#include "Vitals.h"

namespace vitals {

// Material-like color roles. Accent values must match ThemePalette in Model.cpp.
const char* const kStylesheet = R"css(
.app.light {
    --bg: #f3f5f9;
    --surface: #ffffff;
    --surface-variant: #eceff4;
    --on-surface: #1a1c22;
    --on-surface-variant: #5f6472;
    --outline: #dde1e8;
    --on-primary: #ffffff;
    --scrim: rgba(10, 12, 18, 0.42);
    --inverse: #2b2f38;
    --on-inverse: #f1f2f6;
    --tag: #dff3e6;
}
.app.dark {
    --bg: #0f1115;
    --surface: #1a1d24;
    --surface-variant: #262a33;
    --on-surface: #e6e8ee;
    --on-surface-variant: #9aa0ad;
    --outline: #2e333d;
    --on-primary: #11141a;
    --scrim: rgba(0, 0, 0, 0.6);
    --inverse: #e6e8ee;
    --on-inverse: #1a1c22;
}
.app.light.a0 { --primary: #1b8a5a; --primary-container: #c9f0d9; --on-primary-container: #00391f; }
.app.light.a1 { --primary: #3a6ff7; --primary-container: #dbe4ff; --on-primary-container: #0b2a6b; }
.app.light.a2 { --primary: #7a4fd6; --primary-container: #ebddff; --on-primary-container: #2b0f61; }
.app.light.a3 { --primary: #e0603a; --primary-container: #ffdbcf; --on-primary-container: #5a1a06; }
.app.dark.a0 { --primary: #6fdba3; --primary-container: #1f4d36; --on-primary-container: #c9f0d9; }
.app.dark.a1 { --primary: #a9c1ff; --primary-container: #2a3f78; --on-primary-container: #dbe4ff; }
.app.dark.a2 { --primary: #cdb6ff; --primary-container: #4a3383; --on-primary-container: #ebddff; }
.app.dark.a3 { --primary: #ffb59d; --primary-container: #7a2f17; --on-primary-container: #ffdbcf; }

.app {
    background-color: var(--bg);
    color: var(--on-surface);
    font-size: 14px;
    transition: background-color 350ms ease;
}

@keyframes enter-right {
    from { opacity: 0; transform: translateX(36px); }
    to { opacity: 1; transform: translateX(0px); }
}
@keyframes enter-left {
    from { opacity: 0; transform: translateX(-36px); }
    to { opacity: 1; transform: translateX(0px); }
}
@keyframes beat {
    0% { opacity: 1; transform: translateY(0px); }
    15% { opacity: 0.55; transform: translateY(-2px); }
    30% { opacity: 1; transform: translateY(0px); }
    45% { opacity: 0.7; transform: translateY(-1px); }
    100% { opacity: 1; transform: translateY(0px); }
}
@keyframes blink { from { opacity: 1; } to { opacity: 0.2; } }
@keyframes glow { from { opacity: 0.82; } to { opacity: 1; } }

/* Top app bar */
.topbar {
    width: 100%;
    height: 76px;
    padding: 10px 16px 6px 20px;
    gap: 8px;
    align-items: center;
    background-color: var(--bg);
    transition: background-color 350ms ease;
}
.topbar-text { gap: 0px; }
.topbar-sub { font-size: 12px; color: var(--on-surface-variant); }
.topbar-title { font-size: 26px; font-weight: bold; }
.icon-btn {
    width: 40px;
    height: 40px;
    border-radius: 20px;
    align-items: center;
    justify-content: center;
    transition: background-color 150ms ease-out;
}
.icon-btn:hover { background-color: var(--surface-variant); }
.icon-btn:active { background-color: var(--outline); }
.avatar {
    align-items: center;
    justify-content: center;
    background-color: var(--primary);
    transition: background-color 350ms ease, transform 120ms ease-out;
}
.avatar:active { transform: scale(0.92); }
.avatar.small { width: 38px; height: 38px; border-radius: 19px; }
.avatar.large { width: 64px; height: 64px; border-radius: 32px; }
.avatar-text { font-size: 14px; font-weight: bold; color: var(--on-primary); }
.avatar-text.large { font-size: 22px; }

/* Content and pages */
.content { width: 100%; flex-grow: 1; }
.scroll { flex-grow: 1; }
.page { width: 100%; padding: 4px 16px 0px 16px; gap: 14px; }
.page.enter-right { animation: enter-right 0.32s ease-out forwards; }
.page.enter-left { animation: enter-left 0.32s ease-out forwards; }
.page-end { width: 100%; height: 96px; }

.hello { gap: 2px; padding: 4px 4px 0px 4px; }
.hello-title { font-size: 20px; font-weight: bold; }
.hello-sub { font-size: 13px; color: var(--on-surface-variant); }

.chips { width: 100%; gap: 8px; flex-wrap: wrap; }
.chip {
    gap: 6px;
    padding: 7px 12px;
    border-radius: 16px;
    align-items: center;
    background-color: var(--surface);
    border: 1px solid var(--outline);
    transition: background-color 350ms ease;
}
.chip-text { font-size: 13px; font-weight: bold; }

/* Cards */
.card {
    width: 100%;
    border-radius: 24px;
    background-color: var(--surface);
    transition: background-color 350ms ease, opacity 120ms ease-out, transform 120ms ease-out;
}
.card:active { transform: scale(0.98); }
.card-in { width: 100%; padding: 18px; gap: 12px; }
.card.list .card-in { padding: 6px 0px; gap: 0px; }
.card.half { flex-grow: 1; flex-basis: 0px; }
.card-head { width: 100%; gap: 10px; align-items: center; }
.card-title { font-size: 15px; font-weight: bold; }
.card-trailing { font-size: 12px; color: var(--on-surface-variant); }
.caption { font-size: 12px; color: var(--on-surface-variant); }

.big-row { width: 100%; gap: 6px; align-items: flex-end; }
.big-number { font-size: 40px; font-weight: bold; line-height: 1; }
.big-unit { font-size: 14px; color: var(--on-surface-variant); padding: 0px 0px 7px 0px; }
.hr-side { gap: 2px; align-items: flex-end; padding: 0px 0px 6px 0px; }

.bar-track {
    width: 100%;
    height: 10px;
    border-radius: 5px;
    background-color: var(--surface-variant);
    transition: background-color 350ms ease;
}
.bar-fill { border-radius: 5px; background-color: var(--primary); transition: background-color 350ms ease; }
.bar-track.stage { flex-grow: 1; width: 10px; height: 8px; border-radius: 4px; }
.bar-track.stage .bar-fill { border-radius: 4px; }
.app.light .stage.s0 .bar-fill { background-color: #f4893d; }
.app.light .stage.s1 .bar-fill { background-color: #2fb3f0; }
.app.light .stage.s2 .bar-fill { background-color: #5d79f5; }
.app.light .stage.s3 .bar-fill { background-color: #4635b8; }
.app.dark .stage.s0 .bar-fill { background-color: #ffa766; }
.app.dark .stage.s1 .bar-fill { background-color: #6fd0ff; }
.app.dark .stage.s2 .bar-fill { background-color: #8197ff; }
.app.dark .stage.s3 .bar-fill { background-color: #6a5ae0; }

/* Rings */
.rings-row { width: 100%; gap: 20px; align-items: center; }
.legend { gap: 14px; }
.legend-row { gap: 10px; align-items: center; }
.legend-text { gap: 0px; }
.legend-label { font-size: 12px; color: var(--on-surface-variant); }
.legend-value { font-size: 16px; font-weight: bold; }

/* Heart */
.heart-pulse { animation: beat 0.85s linear infinite; }
.live-pill {
    gap: 6px;
    padding: 3px 10px;
    border-radius: 10px;
    align-items: center;
    background-color: var(--surface-variant);
}
.live-dot { width: 7px; height: 7px; border-radius: 4px; background-color: #e8384e; animation: blink 0.7s ease-in-out infinite alternate; }
.live-text { font-size: 10px; font-weight: bold; color: var(--on-surface-variant); }

/* Water */
.glasses { width: 100%; gap: 7px; }
.glass {
    height: 54px;
    border-radius: 9px;
    justify-content: flex-end;
    background-color: var(--surface-variant);
    border: 1px solid var(--outline);
    overflow: hidden;
    transition: background-color 350ms ease, opacity 100ms ease-out;
}
.glass:hover { opacity: 0.85; }
.app.light .glass-fill { background-color: #4b92ff; }
.app.dark .glass-fill { background-color: #3d78d6; }
.glass-fill { border-radius: 7px; }

/* Activity */
.streak {
    width: 100%;
    gap: 12px;
    padding: 14px 18px;
    border-radius: 24px;
    align-items: center;
    background-color: var(--primary-container);
    transition: background-color 350ms ease;
}
.streak-icon {
    width: 40px;
    height: 40px;
    border-radius: 20px;
    align-items: center;
    justify-content: center;
    background-color: var(--primary);
    animation: glow 1.1s ease-in-out infinite alternate;
}
.streak-text { gap: 1px; }
.streak-title { font-size: 16px; font-weight: bold; color: var(--on-primary-container); }
.streak-sub { font-size: 12px; color: var(--on-primary-container); }
.streak-best { font-size: 12px; font-weight: bold; color: var(--on-primary-container); }

.mini-stats { width: 100%; gap: 8px; }
.mini-stat {
    flex-grow: 1;
    flex-basis: 0px;
    padding: 10px 12px;
    gap: 1px;
    border-radius: 16px;
    background-color: var(--surface-variant);
    transition: background-color 350ms ease;
}
.mini-value { font-size: 16px; font-weight: bold; }
.mini-label { font-size: 11px; color: var(--on-surface-variant); }

.section-head { width: 100%; padding: 8px 4px 0px 4px; align-items: center; }
.section-title { font-size: 17px; font-weight: bold; }
.section-title.pad { padding: 8px 4px 0px 4px; }
.section-trailing { font-size: 12px; color: var(--on-surface-variant); }

.workout-row {
    width: 100%;
    padding: 12px 18px;
    gap: 14px;
    align-items: center;
    transition: background-color 200ms ease-out;
}
.workout-row:hover { background-color: var(--surface-variant); }
.workout-row.fresh { animation: enter-right 0.4s ease-out forwards; }
.workout-icon {
    width: 44px;
    height: 44px;
    border-radius: 22px;
    align-items: center;
    justify-content: center;
    background-color: var(--primary-container);
    transition: background-color 350ms ease;
}
.workout-text { gap: 2px; }
.workout-title { font-size: 15px; font-weight: bold; }
.workout-sub { font-size: 12px; color: var(--on-surface-variant); }
.workout-side { gap: 2px; align-items: flex-end; }
.workout-kcal { font-size: 14px; font-weight: bold; }

/* Sleep */
.score-row { width: 100%; gap: 18px; align-items: center; }
.score-text { gap: 4px; }
.score-title { font-size: 20px; font-weight: bold; }
.score-sub { font-size: 14px; color: var(--on-surface-variant); }
.tag {
    padding: 4px 10px;
    border-radius: 10px;
    background-color: var(--primary-container);
    transition: background-color 350ms ease;
}
.tag-text { font-size: 11px; font-weight: bold; color: var(--on-primary-container); }
.pair { width: 100%; gap: 12px; }
.time-value { font-size: 24px; font-weight: bold; }
.stage-list { width: 100%; gap: 10px; }
.stage-row { width: 100%; gap: 10px; align-items: center; }
.stage-name { width: 48px; font-size: 13px; }
.stage-time { width: 60px; font-size: 13px; font-weight: bold; text-align: right; }

/* Profile */
.profile-head { width: 100%; gap: 16px; align-items: center; }
.profile-text { gap: 2px; }
.profile-name { font-size: 20px; font-weight: bold; }
.goal { width: 100%; gap: 6px; }
.goal-head { width: 100%; align-items: center; }
.goal-text { gap: 1px; }
.goal-label { font-size: 14px; font-weight: bold; }
.goal-value { font-size: 14px; font-weight: bold; color: var(--primary); }
.slider { height: 28px; }
.number { width: 150px; }
.segments {
    width: 100%;
    gap: 4px;
    padding: 4px;
    border-radius: 22px;
    background-color: var(--surface-variant);
    transition: background-color 350ms ease;
}
.segment {
    height: 38px;
    gap: 8px;
    border-radius: 19px;
    align-items: center;
    justify-content: center;
    background-color: transparent;
    transition: background-color 220ms ease-out;
}
.segment.on { background-color: var(--primary-container); }
.segment-text { font-size: 14px; font-weight: bold; color: var(--on-surface-variant); }
.segment.on .segment-text { color: var(--on-primary-container); }
.swatches { width: 100%; gap: 14px; padding: 4px 0px; justify-content: center; }
.swatch {
    width: 52px;
    height: 52px;
    border-radius: 26px;
    align-items: center;
    justify-content: center;
    border: 3px solid transparent;
    transition: background-color 200ms ease-out;
}
.swatch.on { border: 3px solid var(--on-surface); }
.swatch-dot { width: 38px; height: 38px; border-radius: 19px; align-items: center; justify-content: center; }
.swatch-dot.a0 { background-color: #1b8a5a; }
.swatch-dot.a1 { background-color: #3a6ff7; }
.swatch-dot.a2 { background-color: #7a4fd6; }
.swatch-dot.a3 { background-color: #e0603a; }
.setting { width: 100%; align-items: center; }
.switch { width: 46px; height: 26px; background-color: var(--primary); }

/* Bottom navigation */
.nav {
    width: 100%;
    height: 80px;
    padding: 0px 0px 6px 0px;
    background-color: var(--surface);
    border: 1px solid var(--outline);
    transition: background-color 350ms ease;
}
.nav-item { height: 100%; gap: 6px; padding: 17px 0px 0px 0px; align-items: center; }
.nav-label { font-size: 12px; color: var(--on-surface-variant); }
.nav-label.active { font-weight: bold; color: var(--on-surface); }

/* FAB, snackbar, sheet */
.fab {
    height: 56px;
    gap: 8px;
    padding: 0px 22px 0px 18px;
    border-radius: 18px;
    align-items: center;
    background-color: var(--primary-container);
    border: 1px solid var(--outline);
    transition: background-color 350ms ease, transform 120ms ease-out;
}
.fab:active { transform: scale(0.95); }
.fab-label { font-size: 15px; font-weight: bold; color: var(--on-primary-container); }

.snackbar {
    height: 52px;
    padding: 0px 8px 0px 18px;
    border-radius: 12px;
    align-items: center;
    background-color: var(--inverse);
}
.snack-text { font-size: 14px; color: var(--on-inverse); }
.snack-action { padding: 8px 12px; border-radius: 16px; transition: background-color 150ms ease-out; }
.snack-action:hover { background-color: rgba(128, 128, 128, 0.25); }
.snack-action-text { font-size: 14px; font-weight: bold; color: var(--primary); }
.app.light .snack-action-text { color: #9fd9b9; }
.app.light.a1 .snack-action-text { color: #a9c1ff; }
.app.light.a2 .snack-action-text { color: #cdb6ff; }
.app.light.a3 .snack-action-text { color: #ffb59d; }

.scrim { background-color: var(--scrim); }
.sheet {
    padding: 10px 18px 22px 18px;
    gap: 10px;
    border-radius: 28px;
    background-color: var(--surface);
    transition: background-color 350ms ease;
}
.sheet-handle { width: 36px; height: 4px; border-radius: 2px; background-color: var(--outline); align-self: center; }
.sheet-title { font-size: 22px; font-weight: bold; padding: 6px 0px 0px 0px; }
.sheet-sub { font-size: 13px; color: var(--on-surface-variant); padding: 0px 0px 6px 0px; }
.card.tile { height: 100%; border-radius: 20px; background-color: var(--surface-variant); }
.card.tile .card-in { padding: 16px; gap: 4px; }
.tile-icon {
    width: 46px;
    height: 46px;
    border-radius: 23px;
    margin: 0px 0px 8px 0px;
    align-items: center;
    justify-content: center;
    background-color: var(--primary-container);
    transition: background-color 350ms ease;
}
.tile-title { font-size: 16px; font-weight: bold; }
.tile-sub { font-size: 12px; color: var(--on-surface-variant); }
.sheet-close {
    width: 100%;
    height: 48px;
    border-radius: 24px;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--outline);
    transition: background-color 150ms ease-out;
}
.sheet-close:hover { background-color: var(--surface-variant); }
.sheet-close-text { font-size: 14px; font-weight: bold; color: var(--primary); }
)css";

} // namespace vitals
