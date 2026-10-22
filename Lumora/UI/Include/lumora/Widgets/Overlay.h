#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Overlay/Positioner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Anchor.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lumora {

/// Overlay layers in ascending paint order (Content bottom, Toast top). A portal renders into one, escaping its ancestor's
/// clip and z-order; later-opened surfaces stack above earlier ones.
enum class Layer : std::uint8_t {
    Content = 0, ///< the normal tree (never a portal target)
    Drawer,      ///< side sheets / panels
    Modal,       ///< dialogs (scrim + focus trap)
    Popover,     ///< dropdowns, comboboxes, pickers (anchored, light-dismiss)
    Menu,        ///< menus / context menus / submenus
    Tooltip,     ///< hover hints (pointer-transparent)
    Toast,       ///< notifications (viewport-corner stack)
    Drag,        ///< the drag ghost - above everything, and excluded from hit-testing outright
    Count,
};

/// Why a floating surface was dismissed, passed to onDismiss so the caller can veto (by not flipping its open state).
enum class DismissReason : std::uint8_t { LightDismiss, Escape, FocusLoss, Explicit };

/// Per-portal behaviour. Dismissal is opt-in: a raw portal stays open until its Show/state unmounts it. capturesPointer /
/// pointerTransparent are hit-test policy set by the widget sugar.
struct PortalOptions {
    bool lightDismiss        = false; ///< a press outside the surface requests dismiss
    bool escapeDismiss       = false; ///< the Escape key requests dismiss (topmost first)
    bool capturesPointer     = false; ///< reserved: a scrim that swallows clicks to content beneath
    bool pointerTransparent  = false; ///< reserved: events pass through (tooltips)
    /// Trap keyboard focus inside this surface while it is topmost (Tab cycles within its content). Off by default; Modal and
    /// Drawer trap regardless (see OverlayRoot::TopmostTrappingContent).
    bool trapsFocus          = false;
    std::function<void(DismissReason)> onDismiss{};
};

/// Optional anchored placement: the Portal registers it with the per-app positioner so content tracks its trigger. Set by
/// Popover etc., empty for a raw Portal.
struct PortalPositioner {
    Ref<detail::AnchorState> anchor;
    Placement                base{};
    float                    offset = 0.f;
    std::vector<Middleware>  middleware;
    Spacing                  viewportPad{};
    std::function<void(const PositionerResult&)> onResult{};
};

namespace detail {

/// Erased blueprint config for the private PortalWidget runtime.
struct PortalConfig {
    Layer                          layer = Layer::Popover;
    std::function<Elem()>          build;
    PortalOptions                  options;
    std::optional<PortalPositioner> positioner; ///< anchored placement for the content root
};
[[nodiscard]] LUMORA_API Ref<Node> BuildPortal(PortalConfig cfg);

}

/// Render `build`'s subtree into overlay `layer` instead of the declaring parent. Ownership (reactivity, Context, OnFrame,
/// disposal) stays with the current Owner, but layout, paint and hit-testing happen in the layer. Wrap in Show/Presence for open/close.
template<typename Builder>
requires std::invocable<Builder> && std::convertible_to<std::invoke_result_t<Builder>, Elem>
[[nodiscard]] Elem Portal(Layer layer, Builder build, PortalOptions options = {}) {
    detail::PortalConfig cfg;
    cfg.layer   = layer;
    cfg.build   = [build = std::move(build)]() -> Elem { return build(); };
    cfg.options = std::move(options);
    return Elem(detail::BuildPortal(std::move(cfg)));
}

/// Imperative open/close handle for a floating surface; pair with `Show(handle.open, ...)` or drive via Open/Close/Toggle.
/// Copies share the signal.
struct OverlayHandle {
    Signal<bool> open{false};

    void Open() const { open = true; }
    void Close() const { open = false; }
    void Toggle() const { open = !open.Peek(); }
    [[nodiscard]] bool IsOpen() const { return open.Peek(); }
};

[[nodiscard]] inline OverlayHandle UseOverlay(bool initiallyOpen = false) {
    return OverlayHandle{Signal<bool>{initiallyOpen}};
}

}
