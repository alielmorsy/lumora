#include "Gallery.h"

namespace gallery {

// Two palettes over one set of variables; the root class picks one, an accent class sets --accent.
const char* const kStylesheet = R"css(
.app.dark {
    --bg: #0f1115;
    --surface: #171a21;
    --surface-2: #1e222b;
    --border: #2a2f3a;
    --fg: #e6e8ee;
    --muted: #8b93a3;
    --hover: #232835;
    --row-alt: #1a1d25;
}
.app.light {
    --bg: #f3f4f7;
    --surface: #ffffff;
    --surface-2: #f6f7fa;
    --border: #dde1e8;
    --fg: #1b1f27;
    --muted: #667085;
    --hover: #e9ecf2;
    --row-alt: #f7f8fb;
}
.accent-blue   { --accent: #4c8dff; --accent-hover: #6a9fff; }
.accent-violet { --accent: #8b5cf6; --accent-hover: #a07cf8; }
.accent-green  { --accent: #22c55e; --accent-hover: #3fd476; }
.accent-amber  { --accent: #f59e0b; --accent-hover: #f7b23b; }

.app {
    background-color: var(--bg);
    color: var(--fg);
    font-size: 14px;
}

/* Sidebar */
.sidebar {
    width: 248px;
    height: 100%;
    padding: 20px 14px;
    gap: 4px;
    background-color: var(--surface);
    border: 1px solid var(--border);
}
.brand { padding: 4px 10px 18px 10px; gap: 2px; }
.brand-title { font-size: 20px; font-weight: bold; color: var(--fg); }
.brand-sub { font-size: 12px; color: var(--muted); }
.nav { gap: 2px; }
.nav-item {
    width: 100%;
    padding: 9px 12px;
    border-radius: 8px;
    background-color: transparent;
    color: var(--muted);
    justify-content: flex-start;
    transition: background-color 120ms ease-out;
}
.nav-item:hover { background-color: var(--hover); color: var(--fg); }
.nav-item.active { background-color: var(--accent); color: #ffffff; }
.theme-toggle {
    padding: 10px 10px;
    border-radius: 8px;
    background-color: var(--surface-2);
    align-items: center;
}

/* Main area */
.main { flex-grow: 1; height: 100%; }
.topbar {
    height: 56px;
    padding: 0px 28px;
    align-items: center;
    border: 1px solid var(--border);
    background-color: var(--surface);
}
.topbar-title { font-size: 16px; font-weight: bold; }
.topbar-hint { font-size: 12px; color: var(--muted); }
.content { flex-grow: 1; }
.content-inner { width: 100%; padding: 28px; }

.section { width: 100%; max-width: 920px; gap: 18px; }
.section-head { gap: 4px; padding: 0px 0px 6px 0px; }
.section-title { font-size: 26px; font-weight: bold; }
.section-desc { font-size: 13px; color: var(--muted); }

/* Cards */
.card {
    width: 100%;
    padding: 18px 20px;
    gap: 14px;
    border-radius: 12px;
    background-color: var(--surface);
    border: 1px solid var(--border);
}
.card-head { gap: 2px; }
.card-title { font-size: 15px; font-weight: bold; }
.card-caption { font-size: 12px; color: var(--muted); }
.card-body { gap: 12px; }

.row { gap: 10px; align-items: center; flex-wrap: wrap; }
.toggle-row {
    padding: 8px 12px;
    gap: 10px;
    border-radius: 8px;
    align-items: center;
    background-color: var(--surface-2);
}
.label { color: var(--fg); }
.muted { font-size: 12px; color: var(--muted); }

/* Buttons and toggles */
.btn { border-radius: 8px; padding: 8px 16px; transition: background-color 120ms ease-out; }
.btn-primary { background-color: var(--accent); color: #ffffff; }
.btn-primary:hover { background-color: var(--accent-hover); }
.btn-primary:active { transform: translateY(1px); }
.btn-outline { color: var(--fg); }
.btn-outline:hover { background-color: var(--hover); }
.btn-ghost { color: var(--accent); }
.btn-ghost:hover { background-color: var(--hover); }
.btn-text { color: var(--muted); }
.switch { background-color: var(--accent); }
.checkbox { color: var(--fg); background-color: var(--accent); border-color: var(--border); }
.splitter { background-color: var(--border); }
.chip-row { gap: 8px; flex-wrap: wrap; }
.chip { background-color: var(--surface-2); border: 1px solid var(--border); color: var(--fg); }
.chip:hover { background-color: var(--hover); }

/* Inputs */
.field {
    padding: 8px 10px;
    border-radius: 8px;
    background-color: var(--surface-2);
    border: 1px solid var(--border);
    color: var(--fg);
}
.field:focus { border-color: var(--accent); }
.editor { line-height: 1.4; }
.number { width: 260px; }

/* Lists */
.list-head { padding: 6px 12px; border-radius: 6px; background-color: var(--surface-2); }
.list-col { font-size: 11px; font-weight: bold; color: var(--muted); }
.vlist { border: 1px solid var(--border); border-radius: 8px; }
.list-row { width: 100%; height: 28px; padding: 0px 12px; align-items: center; }
.list-row.odd { background-color: var(--row-alt); }
.list-row:hover { background-color: var(--hover); }
.list-name { flex-grow: 3; flex-basis: 0px; }
.list-kind { flex-grow: 2; flex-basis: 0px; color: var(--muted); }
.list-size { flex-grow: 1; flex-basis: 0px; text-align: right; color: var(--muted); }
.scroll-box { border: 1px solid var(--border); border-radius: 8px; }
.note-list { width: 100%; padding: 8px; gap: 6px; }
.note { gap: 10px; padding: 8px 10px; border-radius: 6px; background-color: var(--surface-2); align-items: center; }

/* Layout demos */
.split { border: 1px solid var(--border); border-radius: 8px; overflow: hidden; }
.pane { padding: 14px; gap: 4px; background-color: var(--surface-2); }
.pane-title { font-weight: bold; }
.accordion { border: 1px solid var(--border); border-radius: 8px; }
.accordion-header { padding: 10px 12px; }
.accordion-header:hover { background-color: var(--hover); }
.acc-body { padding: 4px 14px 14px 14px; gap: 8px; }
.grid { width: 100%; }
.tile { width: 100%; height: 100%; padding: 10px; border-radius: 8px; justify-content: flex-end; }
.tile-label { font-size: 12px; font-weight: bold; color: #ffffff; }
.tile-a { background-color: var(--accent); }
.tile-b { background-color: #3b4252; }
.tile-c { background-color: #5e6b85; }
.tile-d { background-color: #7c6f9b; }
.crumbs { padding: 8px 10px; border-radius: 8px; background-color: var(--surface-2); }

/* Theming */
.swatch { border-radius: 18px; padding: 8px 18px; color: #ffffff; border: 2px solid transparent; }
.swatch-blue   { background-color: #4c8dff; }
.swatch-violet { background-color: #8b5cf6; }
.swatch-green  { background-color: #22c55e; }
.swatch-amber  { background-color: #f59e0b; }
.swatch.selected { border: 2px solid var(--fg); }
.preview { padding: 16px; gap: 8px; border-radius: 10px; background-color: var(--surface-2); }
.preview-title { font-size: 18px; font-weight: bold; }
)css";

}
