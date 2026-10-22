#include "BenchScenes.h"

#include <lumora/Input/Event.h>
#include <lumora/MountedApp.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <algorithm>
#include <string>
#include <vector>

namespace lumora::bench
{
    namespace
    {
        constexpr float kRowHeight = 24.f;
        constexpr float kSurfaceW = 1920.f;
        constexpr float kSurfaceH = 1080.f;

        /// A flat column of `count` fixed-height tiles; flat on purpose to isolate per-node from per-depth cost.
        [[nodiscard]] Elem TileColumn(int count, const char* tileClass)
        {
            std::vector<Elem> tiles;
            tiles.reserve(static_cast<std::size_t>(std::max(0, count)));
            for (int i = 0; i < count; ++i)
            {
                tiles.push_back(Box(BoxCfg{
                    .layout = {.width = Expand, .height = Fixed(kRowHeight), .padding = Spacing(2.f)},
                    .style = {.fill = Color::Hex(0xFF212126)},
                    .className = tileClass ? std::string(tileClass) : std::string{},
                }));
            }
            return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, tiles);
        }

        /// `groups` sibling groups of `perGroup` tiles, the first tile's fill driven by `fill`. Separates subtree reuse from
        /// frame reuse: an idle frame emits nothing, a one-leaf repaint walks one group's nodes, not the whole tree.
        [[nodiscard]] Elem GroupedTiles(int groups, int perGroup, const Signal<Color>& fill)
        {
            std::vector<Elem> rows;
            rows.reserve(static_cast<std::size_t>(std::max(0, groups)));
            for (int g = 0; g < groups; ++g)
            {
                std::vector<Elem> tiles;
                tiles.reserve(static_cast<std::size_t>(std::max(0, perGroup)));
                for (int t = 0; t < perGroup; ++t)
                {
                    BoxCfg cfg{};
                    cfg.layout = {.width = Expand, .height = Fixed(kRowHeight)};
                    if (g == 0 && t == 0)
                    {
                        cfg.style.fill = fill;
                    }
                    else
                    {
                        cfg.style.fill = Color::Hex(0xFF212126);
                    }
                    tiles.push_back(Box(cfg));
                }
                rows.push_back(Column(ColumnCfg{.layout = {.width = Expand, .height = Wrap}}, tiles));
            }
            return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, rows);
        }

        /// A chain `depth` deep of padded boxes; Refresh, ReadBack, Remeasure and the emit walk recurse over it, so it shows
        /// per-depth cost.
        [[nodiscard]] Elem DeepChain(int depth)
        {
            Elem leaf = Box(BoxCfg{.layout = {.width = Fixed(40.f), .height = Fixed(40.f)},
                                   .style = {.fill = Color::Hex(0xFF3A80FF)}});
            for (int i = 0; i < depth; ++i)
            {
                leaf = Box(BoxCfg{.layout = {.padding = Spacing(1.f)}}, leaf);
            }
            return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, leaf);
        }

        void DispatchMove(MountedApp& app, Vec2 at)
        {
            Event ev{};
            ev.Type = EventType::MouseMove;
            ev.Position = at;
            app.Dispatch(ev);
        }

        void DispatchWheel(MountedApp& app, Vec2 at, float notches)
        {
            Event ev{};
            ev.Type = EventType::Scroll;
            ev.Position = at;
            ev.ScrollDelta = {0.f, notches};
            app.Dispatch(ev);
        }
    } // namespace

    std::vector<Scene> Catalogue(int scale)
    {
        const int tiles = std::max(1, scale);
        const int depth = std::clamp(scale / 8, 8, 256); // recursion depth, not node count
        const int labels = std::max(1, scale / 10);
        const int rows = std::max(1, scale * 10);        // virtual list: logical rows, most never built
        const int cells = std::max(1, scale);

        std::vector<Scene> scenes;

        // 1. Idle floor: nothing changes, so every microsecond and allocation reported is waste.
        scenes.push_back(Scene{
            .name = "static-idle",
            .note = "flat tree, zero mutation: the idle floor",
            .build = [tiles] { return TileColumn(tiles, nullptr); },
        });

        // 2. Same idea, one deep chain: separates per-depth recursion cost from per-node cost.
        scenes.push_back(Scene{
            .name = "static-deep",
            .note = "one deep chain, zero mutation: per-depth recursion cost",
            .build = [depth] { return DeepChain(depth); },
        });

        // 3. One leaf changes per frame in a big tree: the gap from static-idle is the cost of one change; a gap that scales
        // with tree size is the bug.
        {
            const Signal<std::string> caption{std::string("frame 0")};
            scenes.push_back(Scene{
                .name = "one-leaf-write",
                .note = "big tree, one Text rewritten per frame: cost of a single change",
                .build = [tiles, caption]
                {
                    std::vector<Elem> kids;
                    kids.reserve(2);
                    kids.push_back(Text(caption, TextCfg{.size = 13.f}));
                    kids.push_back(TileColumn(tiles, nullptr));
                    return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, kids);
                },
                .step = [caption](MountedApp& app, int frame)
                {
                    app.RunScoped([caption, frame] { caption = "frame " + std::to_string(frame); });
                },
            });
        }

        // 3b. Incremental paint on one nested tree.
        // repaint-one-leaf drives a single Box's fill; its `emit` counter measures subtree reuse and should read a group's
        // worth of nodes, not the tree's.
        // repaint-idle never touches the tree: emit 0, one frame reuse, and its paint time is the floor a static UI pays.
        {
            const Signal<Color> fill{Color::Hex(0xFF212126)};
            const int groups = std::max(1, tiles / 16);
            scenes.push_back(Scene{
                .name = "repaint-one-leaf",
                .note = "nested tree, one leaf's fill rewritten per frame: subtree reuse",
                .build = [groups, fill] { return GroupedTiles(groups, 16, fill); },
                .step = [fill](MountedApp& app, int frame)
                {
                    app.RunScoped([fill, frame]
                    {
                        fill = (frame & 1) ? Color::Hex(0xFF3A80FF) : Color::Hex(0xFF212126);
                    });
                },
            });
            scenes.push_back(Scene{
                .name = "repaint-idle",
                .note = "the same nested tree, never touched: the retained-paint floor",
                .build = [groups, fill] { return GroupedTiles(groups, 16, fill); },
            });
        }

        // 4. Styled tiles with a CSS transition, hovered by a synthetic pointer walking the list: exercises restyle,
        // pseudo-state writes and the animation driver.
        scenes.push_back(Scene{
            .name = "hover-transitions",
            .note = "styled tiles + :hover transition, pointer walking the list",
            .css = ".tile { background-color: #202024; transition: background-color 150ms ease; }\n"
                   ".tile:hover { background-color: #3a80ff; }\n",
            .build = [tiles] { return TileColumn(tiles, "tile"); },
            .step = [tiles](MountedApp& app, int frame)
            {
                // The shell reserves a menubar strip above the content, so aim below y = 0 and walk a band of rows that exist.
                constexpr float kShellTop = 32.f;
                const int reachable = std::max(1, std::min(tiles, 40));
                const int row = frame % reachable;
                DispatchMove(app, Vec2{12.f, kShellTop + static_cast<float>(row) * kRowHeight + kRowHeight * 0.5f});
            },
        });

        // 5. A virtual list far larger than its materialised tree, scrolled every frame: isolates window arithmetic and row reconciliation.
        scenes.push_back(Scene{
            .name = "virtual-list",
            .note = "large logical row count, window scrolled every frame",
            .build = [rows]
            {
                return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}},
                              VirtualList(VirtualListCfg{
                                  .count = rows,
                                  .itemExtent = kRowHeight,
                                  .row = [](int index)
                                  {
                                      return Box(BoxCfg{
                                          .layout = {.width = Expand,
                                                     .height = Fixed(kRowHeight),
                                                     .padding = Spacing(2.f, 6.f)},
                                          .style = {.fill = (index % 2) == 0 ? Color::Hex(0xFF1C1C21)
                                                                             : Color::Hex(0xFF24242A)},
                                      });
                                  },
                                  .layout = {.width = Expand, .height = Expand},
                              }));
            },
            .step = [](MountedApp& app, int frame)
            {
                DispatchWheel(app, Vec2{kSurfaceW * 0.5f, kSurfaceH * 0.5f}, (frame % 120) < 60 ? -1.f : 1.f);
            },
        });

        // 6. Text-heavy static panel, run with the fake measurer then the real FontSystem; the delta is the text subsystem's
        // per-frame cost.
        scenes.push_back(Scene{
            .name = "text-panel",
            .note = "static labels: steady-state shaping and measure cost",
            .text = true,
            .build = [labels]
            {
                std::vector<Elem> lines;
                lines.reserve(static_cast<std::size_t>(labels));
                for (int i = 0; i < labels; ++i)
                {
                    lines.push_back(Row(RowCfg{.layout = {.width = Expand,
                                                          .height = Fixed(kRowHeight),
                                                          .padding = Spacing(2.f, 8.f),
                                                          .gap = 8.f}},
                                        Text("Entity", TextCfg{.size = 13.f}),
                                        Text(std::string("row ") + std::to_string(i), TextCfg{.size = 13.f})));
                }
                return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, lines);
            },
        });

        // 7. Static controls. Text caches its shaped run; Button, Checkbox and Input do not, so this scene shows glyph work
        // on a frame where nothing changed (the real-face gap is FreeType + HarfBuzz).
        scenes.push_back(Scene{
            .name = "controls-idle",
            .note = "static buttons: the uncached glyph-run path",
            .text = true,
            .build = [labels]
            {
                std::vector<Elem> kids;
                kids.reserve(static_cast<std::size_t>(labels));
                for (int i = 0; i < labels; ++i)
                {
                    kids.push_back(Button(ButtonCfg{.label = std::string("Action ") + std::to_string(i)}));
                }
                return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand, .gap = 2.f}}, kids);
            },
        });

        // 8. A grid: the track-sizing pass plus the re-place / re-solve fixpoint loop.
        scenes.push_back(Scene{
            .name = "grid-cells",
            .note = "grid track sizing + cell placement, static",
            .build = [cells]
            {
                std::vector<Elem> items;
                items.reserve(static_cast<std::size_t>(cells));
                for (int i = 0; i < cells; ++i)
                {
                    items.push_back(Box(BoxCfg{.layout = {.width = Expand, .height = Expand},
                                               .style = {.fill = Color::Hex(0xFF2A2A33)}}));
                }
                return Grid(GridCfg{.columns = 8,
                                    .gap = 4.f,
                                    .autoRows = Px(kRowHeight),
                                    .layout = {.width = Expand, .height = Expand}},
                            items);
            },
        });

        return scenes;
    }
} // namespace lumora::bench
