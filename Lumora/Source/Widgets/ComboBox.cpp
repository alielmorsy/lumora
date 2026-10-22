#include <lumora/Widgets/ComboBox.h>

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Overlay/Popover.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Input.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Misc.h>
#include <lumora/Widgets/Painter.h>
#include <lumora/Widgets/Scroll.h>
#include <lumora/Widgets/Text.h>

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        [[nodiscard]] std::string Lower(std::string s)
        {
            for (char& c : s)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }

        // Vector glyphs: the default font lacks the chevron/check codepoints.
        /// Reactive tint: the combo can become disabled after mount, so read inside the paint callback.
        [[nodiscard]] Elem DownChevron(Reactive<Color> color)
        {
            return Painter({.width = Fixed(12.f), .height = Fixed(12.f),
                .onPaint = [color](Paint2D& p)
                {
                    const Vec2 c = p.Bounds().Center();
                    p.FillTriangle({c.x - 4.f, c.y - 2.f}, {c.x + 4.f, c.y - 2.f}, {c.x, c.y + 3.f},
                                   color.Get());
                }});
        }

        [[nodiscard]] Elem Checkmark(Reactive<Color> color)
        {
            return Painter({.width = Fixed(14.f), .height = Fixed(14.f),
                .onPaint = [color](Paint2D& p)
                {
                    const Vec2     c      = p.Bounds().Center();
                    const Vec2     pts[3] = {{c.x - 4.f, c.y}, {c.x - 1.f, c.y + 3.f}, {c.x + 4.f, c.y - 3.f}};
                    p.DrawPolyline(pts, color.Get(), 1.6f, false, LineJoin::Miter, LineCap::Round);
                }});
        }
    } // namespace

    Elem ComboBox::Render()
    {
        const float width = m_cfg.width;
        const int   count = static_cast<int>(m_cfg.options.size());

        // Stretch: the trigger fills its parent and the out-of-flow dropdown is anchored to its live width.
        const Reactive<SizeHint> trigWidth =
            m_cfg.stretch ? Reactive<SizeHint>(Expand) : Reactive<SizeHint>(Fixed(width));

        // Disabled reads go through props: Render() runs once and the combo can be disabled after mount.
        const Reactive<bool> disabled = m_cfg.disabled;

        StyleProps trigStyle = m_cfg.style;
        trigStyle.hoverFill  = Reactive<Color>([disabled, hover = m_cfg.style.hoverFill,
                                                fill = m_cfg.style.fill]
                                               { return disabled.Get() ? fill.Get() : hover.Get(); });
        const Reactive<Color> chevronTint =
            Reactive<Color>([disabled, dim = m_cfg.dim]
                            {
                                const Color d = dim.Get();
                                return disabled.Get() ? d.WithAlpha(d.a / 2) : d;
                            });

        Elem dropdown = Show(m_open, [this, width, count]
        {
            // Indices shown: all when non-editable, the typed-text filter when editable.
            Computed<std::vector<int>> visible([this]
            {
                std::vector<int> out;
                out.reserve(m_cfg.options.size());
                const std::string query = m_cfg.editable ? Lower(m_text()) : std::string{};
                for (int i = 0; i < static_cast<int>(m_cfg.options.size()); ++i)
                {
                    if (query.empty() || Lower(m_cfg.options[i]).find(query) != std::string::npos)
                    {
                        out.push_back(i);
                    }
                }
                return out;
            });

            auto row = [this](int i) -> Elem
            {
                BoxCfg cfg;
                cfg.layout = {.width   = Expand,
                              .height  = Fixed(m_cfg.itemHeight),
                              .padding = Spacing::Symmetric(0.f, 10.f),
                              .cross   = Stretch};
                cfg.style       = {.hoverFill = m_cfg.hover, .radius = 5.f};
                cfg.on.onClick  = [this, i]
                {
                    m_cfg.selected.Set(i);
                    if (m_cfg.editable)
                    {
                        m_text = m_cfg.options[i];
                    }
                    if (m_cfg.onChange)
                    {
                        m_cfg.onChange(i);
                    }
                    m_open = false;
                };

                return Box(std::move(cfg),
                    Row(RowCfg{.layout = {.width = Expand, .height = Expand, .cross = Center, .gap = 10.f}},
                        Text(m_cfg.options[i], {.size = m_cfg.fontSize, .color = m_cfg.textColor}),
                        Spacer(),
                        Show(Computed<bool>([this, i] { return m_cfg.selected.Get() == i; }),
                             [this] { return Checkmark(m_cfg.textColor); })));
            };

            const Reactive<SizeHint> listWidth = m_cfg.stretch
                ? Reactive<SizeHint>([this] { return Fixed(m_anchor.Width()); })
                : Reactive<SizeHint>(Fixed(width));

            Elem list = Column(ColumnCfg{.layout = {.width = listWidth, .cross = Stretch, .gap = 1.f}},
                               Each(visible, [](int i) { return i; }, row));

            Elem body = count > m_cfg.maxVisibleRows
                ? Scroll(ScrollCfg{.axis   = ScrollAxis::Vertical,
                                   .layout = {.width = listWidth, .height = Fixed(m_cfg.maxVisibleRows * m_cfg.itemHeight)}},
                         std::move(list))
                : std::move(list);

            PopoverCfg pc;
            pc.anchor    = m_anchor;
            pc.side      = Side::Bottom;
            pc.align     = Align3::Start;
            pc.offset    = 4.f;
            pc.layer     = Layer::Menu;
            pc.layout    = {.padding = Spacing::All(5.f)};
            pc.style     = {.fill = m_cfg.panelFill, .radius = 8.f, .border = {1.f, m_cfg.panelBorder.Get()}};
            pc.onDismiss = [this](DismissReason) { m_open = false; };
            return Popover(std::move(pc), std::move(body));
        });

        Elem trigger;
        if (m_cfg.editable)
        {
            // Sync edit text with the selection; a partial type leaves selected at -1, so typing is not clobbered.
            Effect([this]
            {
                const int i = m_cfg.selected.Get();
                if (i >= 0 && i < static_cast<int>(m_cfg.options.size()))
                {
                    m_text = m_cfg.options[i];
                }
            });

            auto onType = [this](std::string s)
            {
                m_open = true;
                int match = -1;
                for (int i = 0; i < static_cast<int>(m_cfg.options.size()); ++i)
                {
                    if (m_cfg.options[i] == s)
                    {
                        match = i;
                        break;
                    }
                }
                m_cfg.selected.Set(match);
                if (m_cfg.onChange)
                {
                    m_cfg.onChange(match);
                }
                if (m_cfg.onTextChange)
                {
                    m_cfg.onTextChange(s);
                }
            };

            RowCfg rowCfg;
            rowCfg.layout     = {.width = trigWidth, .margin = m_cfg.layout.margin, .cross = Center};
            rowCfg.style      = trigStyle;
            rowCfg.on.ref     = m_anchor;
            rowCfg.className  = m_cfg.className;
            trigger = Row(std::move(rowCfg),
                Input(InputCfg{
                    .value       = Bind<std::string>(m_text),
                    .color       = m_cfg.textColor,
                    .placeholder = m_cfg.placeholder,
                    .onChange    = std::move(onType),
                    .disabled    = m_cfg.disabled,
                    .layout      = {.width = Expand, .padding = Spacing::Symmetric(8.f, 10.f)},
                    .style       = {.fill = Color::Transparent, .border = {0.f, Color::Transparent}},
                }),
                Box(BoxCfg{.layout = {.padding = Spacing::Symmetric(8.f, 10.f), .cross = Center},
                           .on     = {.onClick = [this] { m_open.Toggle(); }, .disabled = disabled}},
                    DownChevron(chevronTint)));
        }
        else
        {
            BoxCfg cfg;
            cfg.layout    = {.width   = trigWidth,
                             .padding = Spacing::Symmetric(8.f, 10.f),
                             .margin  = m_cfg.layout.margin,
                             .cross   = Stretch};
            cfg.style     = trigStyle;
            cfg.className = m_cfg.className;
            cfg.on.onClick  = [this] { m_open.Toggle(); };
            cfg.on.disabled = disabled;
            cfg.on.ref      = m_anchor;

            trigger = Box(std::move(cfg),
                Row(RowCfg{.layout = {.width = Expand, .cross = Center, .gap = 8.f}},
                    Text([this]
                         {
                             const int i = m_cfg.selected.Get();
                             return (i >= 0 && i < static_cast<int>(m_cfg.options.size()))
                                 ? m_cfg.options[i]
                                 : m_cfg.placeholder.Get();
                         },
                         {.size  = m_cfg.fontSize,
                          .color = Reactive<Color>([this, disabled]() -> Color
                          {
                              const Color dim = m_cfg.dim.Get();
                              const Color text = m_cfg.textColor.Get();
                              if (disabled.Get()) return dim.WithAlpha(dim.a / 2);
                              const int i = m_cfg.selected.Get();
                              return (i >= 0 && i < static_cast<int>(m_cfg.options.size())) ? text : dim;
                          })}),
                    Spacer(),
                    DownChevron(chevronTint)));
        }

        return Box(BoxCfg{.layout = {.width = m_cfg.stretch ? Expand : Wrap, .height = Wrap}},
                   std::move(trigger),
                   std::move(dropdown));
    }
} // namespace lumora
