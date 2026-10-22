#include "Pulse.h"

namespace pulse {

const char* const kStylesheet = R"css(
.app.dark {
    --bg: #0b0e14;
    --surface: #121722;
    --surface-2: #171d2a;
    --border: #212938;
    --fg: #e7ebf3;
    --muted: #7d879a;
    --hover: #1d2533;
    --row-alt: #141a26;
    --accent: #5b8cff;
    --accent-soft: #1a2747;
    --up: #13865f;
    --down: #b5392b;
    --flash: #24427f;
    --recent: #152036;
}
.app.light {
    --bg: #eef1f6;
    --surface: #ffffff;
    --surface-2: #f5f7fb;
    --border: #dfe4ec;
    --fg: #172033;
    --muted: #6b7487;
    --hover: #edf1f7;
    --row-alt: #f8f9fc;
    --accent: #2f6bff;
    --accent-soft: #e3ebff;
    --up: #0e9f6e;
    --down: #e0452f;
    --flash: #c9d9ff;
    --recent: #eef3ff;
}

.app {
    background-color: var(--bg);
    color: var(--fg);
    font-size: 13px;
    transition: background-color 300ms ease;
}

@keyframes blink { from { opacity: 1; } to { opacity: 0.25; } }
@keyframes alarm { from { background-color: #5a1d1d; } to { background-color: #b5392b; } }
@keyframes page-in {
    from { opacity: 0; transform: translateY(14px); }
    to { opacity: 1; transform: translateY(0px); }
}
/* var() is not resolved inside @keyframes, so each theme gets literal stops. */
@keyframes flash-dark-a { from { background-color: #2c4f99; } to { background-color: #152036; } }
@keyframes flash-dark-b { from { background-color: #2c4f99; } to { background-color: #152036; } }
@keyframes flash-light-a { from { background-color: #b9cdff; } to { background-color: #eef3ff; } }
@keyframes flash-light-b { from { background-color: #b9cdff; } to { background-color: #eef3ff; } }
@keyframes breathe { from { opacity: 0.55; } to { opacity: 1; } }

/* Header */
.header {
    width: 100%;
    height: 64px;
    padding: 0px 20px;
    gap: 18px;
    align-items: center;
    background-color: var(--surface);
    border: 1px solid var(--border);
}
.brand { width: 170px; gap: 10px; align-items: center; }
.logo {
    width: 30px;
    height: 30px;
    border-radius: 9px;
    background-color: var(--accent);
    align-items: center;
    justify-content: center;
    animation: breathe 1.4s ease-in-out infinite alternate;
}
.logo-core { width: 10px; height: 10px; border-radius: 5px; background-color: #ffffff; }
.brand-text { gap: 0px; }
.brand-title { font-size: 18px; font-weight: bold; }
.brand-sub { font-size: 11px; color: var(--muted); }

.tabs {
    gap: 2px;
    padding: 3px;
    border-radius: 9px;
    background-color: var(--surface-2);
    border: 1px solid var(--border);
}
.tab {
    padding: 6px 14px;
    border-radius: 7px;
    color: var(--muted);
    background-color: transparent;
    transition: background-color 180ms ease-out;
}
.tab:hover { background-color: var(--hover); color: var(--fg); }
.tab.active { background-color: var(--accent); color: #ffffff; }

.chips { gap: 8px; align-items: center; }
.status {
    gap: 6px;
    padding: 4px 10px;
    border-radius: 11px;
    align-items: center;
    background-color: var(--surface-2);
    border: 1px solid var(--border);
    transition: background-color 250ms ease;
}
.status-dot { width: 8px; height: 8px; border-radius: 4px; background-color: var(--muted); }
.status-text { font-size: 11px; font-weight: bold; color: var(--fg); }
.status.live .status-dot { background-color: #2ed3a5; animation: blink 0.8s ease-in-out infinite alternate; }
.status.idle .status-dot { background-color: #ffc145; }
.status.alert { animation: alarm 0.7s ease-in-out infinite alternate; }
.status.alert .status-dot { background-color: #ffffff; animation: blink 0.35s linear infinite alternate; }
.status.alert .status-text { color: #ffffff; }
.status.calm .status-dot { background-color: #5b8cff; }

.counter { gap: 1px; width: 104px; }
.counter-label { font-size: 10px; font-weight: bold; color: var(--muted); }
.counter-value { font-size: 18px; font-weight: bold; }

.controls { gap: 14px; align-items: center; }
.btn { border-radius: 8px; padding: 7px 14px; transition: background-color 150ms ease-out; }
.btn.primary { background-color: var(--accent); color: #ffffff; width: 84px; }
.btn.primary:hover { background-color: #7aa2ff; }
.btn.primary.paused { background-color: #13865f; }
.speed { gap: 2px; }
.speed-head { width: 150px; }
.mini-label { font-size: 10px; font-weight: bold; color: var(--muted); }
.mini-value { font-size: 10px; font-weight: bold; color: var(--accent); }
.theme { gap: 8px; align-items: center; }
.switch { width: 38px; height: 20px; background-color: var(--accent); }

.clock { gap: 0px; width: 104px; align-items: flex-end; }
.clock-time { font-size: 15px; font-weight: bold; }
.clock-sub { font-size: 10px; color: var(--muted); }

/* Pages */
.page {
    width: 100%;
    flex-grow: 1;
    padding: 14px;
    gap: 14px;
    animation: page-in 0.35s ease-out forwards;
}
.col-main { flex-grow: 1; height: 100%; gap: 12px; }
.col-side { width: 420px; height: 100%; gap: 12px; }
.col-wide { width: 560px; height: 100%; gap: 12px; }
.bottom-row { width: 100%; height: 262px; gap: 12px; align-items: stretch; }

.panel {
    padding: 12px 14px;
    gap: 8px;
    border-radius: 12px;
    background-color: var(--surface);
    border: 1px solid var(--border);
}
.panel.grow { flex-grow: 1; }
.panel.race-panel { width: 470px; }
.panel.heat-panel { height: 250px; }
.panel.vitals { height: 350px; }
.panel-head { width: 100%; align-items: center; }
.panel-title { font-size: 13px; font-weight: bold; }
.panel-caption { font-size: 11px; color: var(--muted); }
.panel-body { width: 100%; }
/* flex-grow on a child of a Wrap-height column collapses it to 0, so only growing panels grow their body. */
.panel.grow > .panel-body, .panel.vitals > .panel-body, .panel.heat-panel > .panel-body, .bottom-row .panel-body { flex-grow: 1; }

/* Metric cards */
.card {
    width: 100%;
    height: 100%;
    padding: 10px 12px;
    gap: 2px;
    border-radius: 12px;
    background-color: var(--surface);
    border: 1px solid var(--border);
    transition: background-color 150ms ease-out;
}
.card:hover { background-color: var(--surface-2); }
.metric-head { width: 100%; gap: 6px; align-items: center; }
.metric-dot { width: 8px; height: 8px; border-radius: 4px; }
.c0 { background-color: #5b8cff; }
.c1 { background-color: #2ed3a5; }
.c2 { background-color: #ff7a59; }
.c3 { background-color: #b57bff; }
.c4 { background-color: #ffc145; }
.c5 { background-color: #38bdf8; }
.metric-name { font-size: 11px; color: var(--muted); }
.delta {
    padding: 1px 6px;
    border-radius: 8px;
    transition: background-color 400ms ease;
}
.delta.up { background-color: var(--up); }
.delta.down { background-color: var(--down); }
.delta-text { font-size: 10px; font-weight: bold; color: #ffffff; }
.metric-value-row { gap: 4px; align-items: flex-end; }
.metric-value { font-size: 22px; font-weight: bold; }
.metric-unit { font-size: 11px; color: var(--muted); padding: 0px 0px 4px 0px; }

/* Gauges */
.gauge { width: 100%; height: 100%; align-items: center; gap: 0px; }
.gauge-label { font-size: 11px; font-weight: bold; color: var(--muted); }

/* Bar race */
.race-row { gap: 10px; align-items: center; }
.race-rank { width: 24px; font-size: 11px; font-weight: bold; color: var(--muted); }
.race-name { width: 64px; font-size: 12px; }
.race-track {
    flex-grow: 1;
    height: 14px;
    border-radius: 7px;
    background-color: var(--surface-2);
}
.race-fill { border-radius: 7px; }
.race-value { width: 64px; font-size: 12px; font-weight: bold; text-align: right; }

/* Event log */
.log { gap: 4px; }
.log-head { width: 100%; padding: 6px 10px; border-radius: 6px; background-color: var(--surface-2); }
.lcol { font-size: 10px; font-weight: bold; color: var(--muted); }
.vlist { border-radius: 8px; }
.log-row { width: 100%; height: 26px; padding: 0px 10px; align-items: center; }
.log-row:hover { background-color: var(--hover); }
.log-row.recent { background-color: var(--recent); }
.app.dark .log-row.flash-a { animation: flash-dark-a 0.9s ease-out forwards; }
.app.dark .log-row.flash-b { animation: flash-dark-b 0.9s ease-out forwards; }
.app.light .log-row.flash-a { animation: flash-light-a 0.9s ease-out forwards; }
.app.light .log-row.flash-b { animation: flash-light-b 0.9s ease-out forwards; }
.ltime { width: 92px; font-size: 11px; color: var(--muted); }
.llvl { width: 52px; }
.lsrc { width: 72px; font-size: 11px; color: var(--muted); }
/* Text ignores flex-shrink, so the message column gets a definite width per layout for the ellipsis. */
/* Ellipsis only triggers via line-clamp truncation, not a single nowrap line, so wrap and clamp to 1. */
.lmsg { width: 150px; font-size: 12px; text-overflow: ellipsis; -webkit-line-clamp: 1; }
.col-wide .lmsg { width: 300px; }
.lvl { padding: 1px 6px; border-radius: 5px; }
.lvl.info { background-color: var(--accent-soft); }
.lvl.warn { background-color: #7a5a12; }
.lvl.crit { background-color: #b5392b; }
.lvl-text { font-size: 9px; font-weight: bold; }
.lvl.info .lvl-text { color: var(--accent); }
.lvl.warn .lvl-text { color: #ffd479; }
.lvl.crit .lvl-text { color: #ffffff; }
.log-row.critical .lmsg { color: #ff7a59; }

/* Metric table */
.table { width: 100%; gap: 0px; }
.table-head { width: 100%; height: 26px; padding: 0px 10px; gap: 8px; align-items: center; border-radius: 6px; background-color: var(--surface-2); }
.tcol { font-size: 10px; font-weight: bold; color: var(--muted); }
.table-row { width: 100%; height: 30px; padding: 0px 10px; gap: 8px; align-items: center; }
.table-row.odd { background-color: var(--row-alt); }
.tname { flex-grow: 3; flex-basis: 0px; gap: 8px; align-items: center; }
.trow-name { font-size: 12px; }
.tval { font-size: 12px; text-align: right; }
.tnum { font-weight: bold; }
.tup { color: #2ed3a5; }
.tdown { color: #ff7a59; }
)css";

} // namespace pulse
