#include <lumora/Widgets/NumberField.h>

#include <lumora/Input/Event.h>
#include <lumora/Paint/Paint2D.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Input.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Painter.h>
#include <lumora/Widgets/Text.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace lumora
{
    namespace
    {
        /// Fixed precision so a scrubbed value does not jitter in width; a trailing "-0" is normalised to "0".
        [[nodiscard]] std::string Format(double value, int decimals)
        {
            const int places = std::clamp(decimals, 0, 9);
            std::string out = std::format("{:.{}f}", value, places);
            if (out.find_first_not_of("-0.") == std::string::npos && out.find('-') == 0)
            {
                out.erase(out.begin());
            }
            return out;
        }

        [[nodiscard]] bool Parse(const std::string& text, double& out)
        {
            std::size_t index = 0;
            try
            {
                const double parsed = std::stod(text, &index);
                while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
                {
                    ++index;
                }
                if (index != text.size())
                {
                    return false;
                }
                out = parsed;
                return true;
            }
            catch (...)
            {
                return false; // std::stod is the one parse here; a bad edit reverts rather than writes 0
            }
        }

        [[nodiscard]] double Quantise(double value, double snap)
        {
            return snap > 0.0 ? std::round(value / snap) * snap : value;
        }

        /// Value change per pixel. Explicit step wins; a bounded field crosses its range in ~200px, an
        /// unbounded one moves by a fraction of its magnitude.
        [[nodiscard]] double PerPixel(const NumberFieldCfg& cfg, double value)
        {
            if (cfg.step > 0.0)
            {
                return cfg.step;
            }
            if (cfg.min && cfg.max && *cfg.max > *cfg.min)
            {
                return (*cfg.max - *cfg.min) / 200.0;
            }
            return std::max(0.01, std::abs(value) * 0.01);
        }

        [[nodiscard]] double Clamp(const NumberFieldCfg& cfg, double value)
        {
            if (cfg.min) value = std::max(value, *cfg.min);
            if (cfg.max) value = std::min(value, *cfg.max);
            return value;
        }

        // Parts carry fixed class names and the caller's list stays on the row, so selectors like
        // `.num.x .numfield-label` work (concatenating would make `num x-label` for two classes).
        constexpr const char* kLabelClass = "numfield-label";
        constexpr const char* kLabelTextClass = "numfield-labeltext";
        constexpr const char* kValueClass = "numfield-value";
        constexpr const char* kValueTextClass = "numfield-valuetext";
        constexpr const char* kInputClass = "numfield-input";
    } // namespace

    void NumberField::Commit(double value, bool final)
    {
        const double next = Clamp(m_cfg, Quantise(value, m_cfg.snap));
        if (next != m_cfg.value.Get())
        {
            m_cfg.value.Set(next);
            if (m_cfg.onChange) m_cfg.onChange(next);
        }
        if (final && m_cfg.onCommit) m_cfg.onCommit(next);
    }

    void NumberField::CloseEditor(const std::string& text)
    {
        if (!m_editing.Peek())
        {
            return; // already closed by Enter or Escape; a trailing blur must not commit twice
        }
        double parsed = 0.0;
        if (Parse(text, parsed))
        {
            Commit(parsed, true);
        }
        m_editing = false; // a text that does not parse reverts, never writes 0
    }

    void NumberField::OpenEditor(std::string_view typed)
    {
        if (m_cfg.disabled.Get() || m_editing.Peek())
        {
            return;
        }
        m_text = typed.empty() ? Format(m_cfg.value.Get(), m_cfg.decimals) : std::string(typed);
        m_editing = true;
    }

    void NumberField::Step(int direction, KeyMods mods)
    {
        if (m_cfg.disabled.Get())
        {
            return;
        }
        double amount = m_cfg.step > 0.0 ? m_cfg.step : PerPixel(m_cfg, m_cfg.value.Get());
        if (Has(mods, KeyMods::Shift)) amount *= 10.0;
        if (Has(mods, KeyMods::Alt)) amount *= 0.1;
        Commit(m_cfg.value.Get() + amount * static_cast<double>(direction), true);
    }

    Elem NumberField::Render()
    {
        const int decimals = m_cfg.decimals;
        const Reactive<bool> disabled = m_cfg.disabled;

        // Read through the Bind inside the child's reactive prop, never here: it would subscribe the whole
        // component and rebuild the subtree (dropping the caret). `disabled` is forwarded the same way.
        Str shown = [this, decimals] { return Format(m_cfg.value.Get(), decimals); };

        // Always wired, gated by the prop: `DispatchInteract` drops events on a disabled node, so
        // attaching conditionally would bake in the mount-time answer.
        InteractProps scrub{};
        scrub.disabled = disabled;
        {
            scrub.onPress = [this](PointerInfo)
            {
                if (m_cfg.disabled.Get())
                {
                    return;
                }
                m_dragging = true;
                m_moved = false;
                m_dragValue = m_cfg.value.Get();
            };
            scrub.onMove = [this](PointerInfo p)
            {
                if (m_cfg.disabled.Get() || !m_dragging || p.delta.x == 0.f)
                {
                    return;
                }
                m_moved = true;

                double scale = 1.0;
                if (Has(p.modifiers, KeyMods::Shift)) scale *= 10.0;
                if (Has(p.modifiers, KeyMods::Alt)) scale *= 0.1;

                m_dragValue += static_cast<double>(p.delta.x) * PerPixel(m_cfg, m_dragValue) * scale;
                // Clamp the accumulator, not just the written value, or a drag past the max must be dragged back first.
                m_dragValue = Clamp(m_cfg, m_dragValue);
                Commit(m_dragValue, false);
            };
            scrub.onRelease = [this](PointerInfo)
            {
                if (m_cfg.disabled.Get() || !m_dragging)
                {
                    return;
                }
                m_dragging = false;
                if (m_moved)
                {
                    Commit(m_dragValue, true);
                    return;
                }
                // A press that never moved is a click: hand the row to the text editor instead.
                OpenEditor();
            };
        }

        Elem handle = Box(
            {
                .layout = {.cross = Align::Center},
                .on = std::move(scrub),
                .className = std::string(kLabelClass),
            },
            Text(m_cfg.label, {.color = m_cfg.labelColor, .className = std::string(kLabelTextClass)}));

        Elem body = Show(
            m_editing,
            [this]
            {
                return Input({
                    .value = m_text,
                    // Enter/Escape end the session deliberately: return keyboard focus to the row so Tab continues
                    // to the next row. A blur ends it too, but focus has already moved.
                    .onSubmit = [this](std::string text)
                    {
                        m_returnFocus = true;
                        CloseEditor(text);
                    },
                    .onCancel = [this]
                    {
                        m_returnFocus = true;
                        m_editing = false;
                    },
                    // Focus moving to another row ends this edit, so clicked rows do not stay open.
                    .onBlur = [this](std::string text) { CloseEditor(text); },
                    .autofocus = true,
                    .layout = {.width = Expand},
                    .className = std::string(kInputClass),
                });
            },
            [this, shown, disabled]
            {
                // The display node is the row's Tab stop: the Input exists only while typing, so otherwise the
                // row would contribute nothing to the focus ring.
                InteractProps on{};
                on.disabled = disabled;
                {
                    on.onClick = [this] { OpenEditor(); };
                    // Focus lands on the row, not an editor (type to replace, Enter to edit, arrows to nudge), like a spreadsheet cell.
                    on.onFocus = [this](bool) { m_returnFocus = false; };
                    on.onKeyDown = [this](KeyInfo key)
                    {
                        switch (key.key)
                        {
                        case Key::Enter:
                        case Key::Space:
                            OpenEditor();
                            return true;
                        case Key::Up:
                            Step(+1, key.modifiers);
                            return true;
                        case Key::Down:
                            Step(-1, key.modifiers);
                            return true;
                        default:
                            return false; // Tab stays unconsumed: it is the focus engine's key
                        }
                    };
                    on.onTextInput = [this](std::string_view text)
                    {
                        const char first = text.empty() ? '\0' : text.front();
                        const bool numeric = std::isdigit(static_cast<unsigned char>(first)) || first == '-' ||
                            first == '+' || first == '.';
                        if (!numeric)
                        {
                            return false;
                        }
                        OpenEditor(text); // type-to-replace, like a spreadsheet cell
                        return true;
                    };
                    on.focusPolicy = m_cfg.focusPolicy;
                    on.tabIndex = m_cfg.tabIndex;
                    on.autofocus = m_returnFocus;
                }
                return Box(
                    {
                        .layout = {.width = Expand, .cross = Align::Center},
                        .on = std::move(on),
                        .className = std::string(kValueClass),
                    },
                    Text(shown, {.className = std::string(kValueTextClass)}));
            });

        return Row({.layout = m_cfg.layout, .className = m_cfg.className},
                   std::move(handle), std::move(body));
    }

    Elem Slider::Render()
    {
        const double lo = m_cfg.min;
        const double hi = m_cfg.max > m_cfg.min ? m_cfg.max : m_cfg.min + 1.0;
        const double span = hi - lo;

        constexpr float kThumbRadius = 6.f;

        // A press or drag on the track jumps the value to the pointer, using the rect the paint pass recorded.
        const auto write = [this, lo, hi](Vec2 surfacePos, bool final)
        {
            const float trackWidth = m_track.width - 2.f * kThumbRadius;
            if (trackWidth <= 0.f)
            {
                return;
            }
            const double t = std::clamp(static_cast<double>(surfacePos.x - (m_track.x + kThumbRadius)) /
                                        static_cast<double>(trackWidth),
                                        0.0, 1.0);
            double value = lo + t * (hi - lo);
            if (m_cfg.step > 0.0)
            {
                value = lo + std::round((value - lo) / m_cfg.step) * m_cfg.step;
            }
            value = std::clamp(value, lo, hi);
            if (value != m_cfg.value.Get())
            {
                m_cfg.value.Set(value);
                if (m_cfg.onChange) m_cfg.onChange(value);
            }
            if (final && m_cfg.onCommit) m_cfg.onCommit(value);
        };

        const Reactive<Color> trackColor = m_cfg.track;
        const Reactive<Color> fillColor = m_cfg.fill;

        // Wired unconditionally, gated by the prop (see NumberField::Render).
        InteractProps on{};
        on.disabled = m_cfg.disabled;
        {
            on.onPress = [this, write](PointerInfo p)
            {
                m_dragging = true;
                write(p.position, false);
            };
            on.onMove = [this, write](PointerInfo p)
            {
                if (m_dragging) write(p.position, false);
            };
            on.onRelease = [this, write](PointerInfo p)
            {
                if (!m_dragging)
                {
                    return;
                }
                m_dragging = false;
                write(p.position, true);
            };
        }

        // Wrapped in a Box so `className` reaches the style engine: a Painter has no class of its own.
        return Box(
            {.layout = m_cfg.layout, .className = m_cfg.className},
            Painter({
                .width = Expand,
                .height = Fixed(18.f),
                .onPaint =
                [this, lo, span, trackColor, fillColor](Paint2D& p)
                {
                    const Rect b = p.Bounds();
                    const float y = b.y + b.height * 0.5f;
                    const double t = span > 0.0 ? std::clamp((m_cfg.value.Get() - lo) / span, 0.0, 1.0) : 0.0;

                    m_track = p.SurfaceBounds();

                    const Color track = trackColor.Get().a != 0 ? trackColor.Get() : Color::Rgba(90, 90, 96, 255);
                    const Color fill = fillColor.Get().a != 0 ? fillColor.Get() : Color::Rgba(180, 180, 188, 255);

                    const float trackX = b.x + kThumbRadius;
                    const float trackWidth = std::max(0.f, b.width - 2.f * kThumbRadius);

                    p.FillRect(Rect{trackX, y - 2.f, trackWidth, 4.f}, track, 2.f);
                    p.FillRect(Rect{trackX, y - 2.f, trackWidth * static_cast<float>(t), 4.f}, fill, 2.f);
                    p.FillCircle(Vec2{trackX + trackWidth * static_cast<float>(t), y}, kThumbRadius, fill);
                },
                .on = std::move(on),
            }));
    }
} // namespace lumora
