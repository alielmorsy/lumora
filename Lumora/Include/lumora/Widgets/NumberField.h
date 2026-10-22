#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace lumora
{
    /// A numeric row: drag the label to scrub, click the value to type it. `onChange` fires continuously; `onCommit` once at
    /// gesture end (release, Enter, focus loss), so callers can open an undo transaction on onChange and close it on onCommit.
    struct NumberFieldCfg
    {
        Bind<double>          value;
        std::optional<double> min{};
        std::optional<double> max{};

        /// Value change per pixel of horizontal drag. 0 derives it from the range (1/200th) or, unbounded, from the value's magnitude.
        /// A rate, not a grid: Alt-scrubbing must reach values between steps, so it is not quantised to `step`. Snapping is `snap`, opt-in.
        double step = 0.0;
        double snap = 0.0; ///< round every written value to a multiple of this; 0 == continuous

        int decimals = 3; ///< display precision; 0 renders as an integer

        Str             label{};                        ///< the scrub handle: "X", "Intensity"
        Reactive<Color> labelColor = Color::Transparent; ///< unset -> CSS / inherited

        std::function<void(double)> onChange{};
        std::function<void(double)> onCommit{};

        /// Reactive: read-only must follow without a rebuild, since rebuilding mid-drag drops the gesture. A plain `bool` binds as a static prop.
        Reactive<bool> disabled = false;
        FocusPolicy focusPolicy = FocusPolicy::Tab;
        int         tabIndex    = 0;

        LayoutProps           layout{};
        Reactive<std::string> className{}; ///< the row; the label and the value carry `-label`/`-value`
    };

    /// Composed component (Row + scrub Box + Text/Input), like ComboBox.
    class LUMORA_API NumberField
    {
    public:
        explicit NumberField(NumberFieldCfg cfg) : m_cfg(std::move(cfg)) {}

        [[nodiscard]] Elem Render();

    private:
        void Commit(double value, bool final);

        /// End a typing session: write `text` if it parses and return the row to its label. Enter and focus loss both land here.
        void CloseEditor(const std::string& text);

        /// Start a typing session; `typed` seeds the editor with a keystroke (type-to-replace), empty seeds the formatted value.
        void OpenEditor(std::string_view typed = {});

        /// Keyboard nudge: one `step` (derived if unset) per press, scaled by the drag modifiers (Shift coarse, Alt fine).
        void Step(int direction, KeyMods mods);

        NumberFieldCfg      m_cfg;
        Signal<bool>        m_editing{false};
        Signal<std::string> m_text{};
        bool                m_dragging  = false;
        bool                m_moved     = false; ///< a press with no movement is a click-to-type
        double              m_dragValue = 0.0;
        /// Set when Enter/Escape closed the editor: the display node re-mounts with `autofocus` to keep focus, and that focus must
        /// not reopen the editor.
        bool                m_returnFocus = false;
    };

    struct SliderCfg
    {
        Bind<double> value;
        double       min = 0.0;
        double       max = 1.0;
        double       step = 0.0; ///< 0 == continuous

        std::function<void(double)> onChange{};
        std::function<void(double)> onCommit{};

        Reactive<bool> disabled = false; ///< reactive, for NumberFieldCfg::disabled's reason
        FocusPolicy focusPolicy = FocusPolicy::Tab;
        int         tabIndex    = 0;

        LayoutProps           layout{};
        Reactive<Color>       track = Color::Transparent; ///< unset -> a neutral derived from `fill`
        Reactive<Color>       fill  = Color::Transparent; ///< unset -> the framework accent
        Reactive<std::string> className{};
    };

    /// Track + thumb over the same value contract as NumberField, for a field that declares a range.
    class LUMORA_API Slider
    {
    public:
        explicit Slider(SliderCfg cfg) : m_cfg(std::move(cfg)) {}

        [[nodiscard]] Elem Render();

    private:
        SliderCfg m_cfg;
        /// The track's last painted rect in surface space; recorded by the paint callback (the only place a Painter's rect is
        /// reachable) for the pointer handlers.
        Rect m_track{};
        bool m_dragging = false;
    };
}
