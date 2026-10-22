#pragma once

// Dock/Dock.h - model-driven docking: a DockController owns a reactive Signal<DockTree> that is projected into
// ordinary Row/Column splits, tab groups and an absolute float overlay. Panel content is a builder re-run on every
// projection, so panel state should live in Signals the builder closes over.

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumora
{
    /// Split direction (also a Splitter's drag axis).
    enum class DockAxis : std::uint8_t { Horizontal, Vertical };

    /// Drop zones of a leaf: the four edges split it, the centre tabs into it.
    enum class DockSide : std::uint8_t { Left, Right, Top, Bottom, Center };

    /// Stable panel identity: 64-bit hash of the source id string. Value 0 is the "no panel" sentinel.
    struct PanelId
    {
        std::uint64_t value = 0;

        constexpr PanelId() = default;
        explicit PanelId(std::string_view s) : value(Hash(s)) {}

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return value; }
        [[nodiscard]] constexpr bool operator==(const PanelId&) const noexcept = default;

    private:
        [[nodiscard]] static std::uint64_t Hash(std::string_view s) noexcept
        {
            const std::uint64_t h = std::hash<std::string_view>{}(s);
            return h == 0 ? 1 : h;
        }
    };

    // The model: a value-semantic recursive tree; a node is a Tabs leaf or a Split (children + parallel flex weights).

    struct DockNode
    {
        enum class Kind : std::uint8_t { Tabs, Split } kind = Kind::Tabs;

        DockAxis              axis = DockAxis::Horizontal;
        std::vector<DockNode> children;
        std::vector<float>    weights; ///< parallel to children; flex-grow shares

        std::vector<PanelId> panels;
        int                  active = 0;

        [[nodiscard]] bool IsSplit() const noexcept { return kind == Kind::Split; }
        [[nodiscard]] bool IsTabs() const noexcept { return kind == Kind::Tabs; }

        [[nodiscard]] static DockNode Tabs(std::vector<PanelId> panels, int active = 0)
        {
            DockNode n;
            n.kind = Kind::Tabs;
            n.panels = std::move(panels);
            n.active = active;
            return n;
        }
        [[nodiscard]] static DockNode Split(DockAxis axis, std::vector<DockNode> children,
                                            std::vector<float> weights = {})
        {
            DockNode n;
            n.kind = Kind::Split;
            n.axis = axis;
            n.children = std::move(children);
            n.weights = std::move(weights);
            if (n.weights.size() != n.children.size())
            {
                n.weights.assign(n.children.size(), 1.f);
            }
            return n;
        }
    };

    /// A detached subtree positioned absolutely over the docked area.
    struct FloatWindow
    {
        DockNode root;
        Rect     frame{}; ///< absolute position + size, in surface pixels
    };

    /// Docked area plus floating windows (drawn on top, in order).
    struct DockTree
    {
        DockNode                 root = DockNode::Tabs({});
        std::vector<FloatWindow> floats;
    };

    /// Every panel in the tree, docked first then each float, in document order.
    [[nodiscard]] LUMORA_API std::vector<PanelId> CollectPanels(const DockTree& tree);

    /// What a tab drag landing on no guide does: InWindow floats it over this dock (default); Detach hands it to
    /// DockDetachHooks::onDetach.
    enum class DockFloatPolicy : std::uint8_t { InWindow, Detach };

    struct DockBehavior
    {
        DockFloatPolicy floats       = DockFloatPolicy::InWindow;
        /// False locks the dock: no guide cross, no splitting or tabbing by drag, and no external drops.
        bool            acceptsDrops = true;
    };

    /// What a host does with a tab the dock will not dock internally. Positions are this dock's surface pixels.
    struct DockDetachHooks
    {
        /// Every move while the dragged tab is outside the workspace; nullopt when it comes back or the drag ends.
        std::function<void(PanelId, std::optional<Vec2>)> onDragOutside;

        /// A dragged tab was released outside the workspace, off a guide under Detach, or on a locked dock. True: the host
        /// took the panel and the dock removes it. False: the dock keeps it (floats it under InWindow if unlocked, else cancels).
        std::function<bool(PanelId, Vec2 position, Vec2 size)> onDetach;
    };

    /// A panel's title and content builder; the builder runs on every (re)projection, so keep panel state in Signals.
    struct DockPanel
    {
        std::string           title;
        std::function<Elem()> build;
        bool                  closable = true;
    };

    /// Maps panel id to DockPanel and keeps the source string per id so serialize round-trips names.
    class LUMORA_API DockRegistry
    {
    public:
        PanelId Register(std::string_view id, DockPanel panel);

        [[nodiscard]] const DockPanel* Find(PanelId id) const;
        [[nodiscard]] std::string_view SourceOf(PanelId id) const;

    private:
        std::unordered_map<std::uint64_t, DockPanel>   m_panels;
        std::unordered_map<std::uint64_t, std::string> m_sources;
    };

    /// Dock chrome colours and metrics (neutral dark defaults). The active tab fills with TabActive (set it to the
    /// panel background so the tab merges with its pane) under a 2px Accent line.
    struct DockTheme
    {
        Color TabBar      = Color::Rgba(25, 28, 34, 255);
        Color TabIdle     = Color::Transparent;
        Color TabHover    = Color::Rgba(44, 49, 58, 160);
        Color TabActive   = Color::Rgba(40, 44, 52, 255);
        Color Accent      = Color::Rgba(97, 175, 239, 255);
        Color Text        = Color::Rgba(220, 223, 228, 255);
        Color TextDim     = Color::Rgba(130, 137, 150, 255);
        Color Splitter    = Color::Rgba(58, 64, 74, 255);
        Color DropZone    = Color::Rgba(97, 175, 239, 45);
        Color DropZoneHot = Color::Rgba(97, 175, 239, 160);
        Color GuideBg     = Color::Rgba(33, 37, 43, 235);
        Color FloatBg     = Color::Rgba(40, 44, 52, 255);
        Color Border      = Color::Rgba(58, 64, 74, 255);
        float TabBarHeight = 30.f;
        float SplitterSize = 6.f;
        float MinPanePx    = 60.f;
        float FloatMinW    = 180.f;
        float FloatMinH    = 120.f;
    };

    struct DockState; ///< private projection/interaction state (Dock.cpp)

    /// Owns the dock model, registry and transient drag state; exposes the programmatic edits the pointer performs.
    /// Copies share one DockState.
    class LUMORA_API DockController
    {
    public:
        DockController();
        explicit DockController(DockTree initial);
        ~DockController();
        DockController(const DockController&);
        DockController(DockController&&) noexcept;
        DockController& operator=(const DockController&);
        DockController& operator=(DockController&&) noexcept;

        [[nodiscard]] Signal<DockTree>& Model();
        [[nodiscard]] DockRegistry&     Registry();
        [[nodiscard]] DockTheme&        Theme();

        PanelId Panel(std::string_view id, std::string title, std::function<Elem()> build,
                      bool closable = true);

        void DockTo(PanelId panel, std::vector<int> targetPath, DockSide side);
        void Tabify(PanelId panel, std::vector<int> targetPath);
        void Float(PanelId panel, Rect frame);
        void Close(PanelId panel);
        void SetActive(std::vector<int> tabsPath, int index);
        void Attach(DockNode subtree, DockSide side = DockSide::Right);

        void SetDetachHooks(DockDetachHooks hooks);

        void SetBehavior(DockBehavior behavior);
        [[nodiscard]] const DockBehavior& Behavior() const;

        /// True when a tab released at `position` (this dock's surface pixels) would land on a guide square.
        [[nodiscard]] bool CanAcceptExternal(Vec2 position) const;

        /// Dock `panel` from another dock at `position` using the guide square under it, exactly as an internal
        /// drag would. False (model untouched) when the dock is locked or no guide is under `position`.
        bool AcceptExternal(PanelId panel, Vec2 position);

        /// Show this dock's drag overlay (guide cross + ghost titled from the registry) at `position` for a drag in another
        /// window; nullopt ends it. No-op on a locked dock or during its own drag.
        void PreviewExternal(PanelId panel, std::optional<Vec2> position);

        [[nodiscard]] std::vector<std::byte> Serialize() const;
        /// Replace the model from `data`. Returns false on a malformed blob; unknown panel ids are
        /// dropped. On success the model Signal is re-seeded.
        bool Restore(std::span<const std::byte> data);

    private:
        Ref<DockState> m_state;
        friend LUMORA_API Elem Dock(DockController&);
    };

    /// Projects the controller's DockTree onto widgets; fills its parent and re-projects when the model changes.
    [[nodiscard]] LUMORA_API Elem Dock(DockController& controller);

    // Draggable handle between two flex siblings; onCommit(prevWeight, nextWeight) fires on release.

    struct SplitterCfg
    {
        DockAxis                          axis      = DockAxis::Horizontal;
        float                             thickness = 6.f;
        float                             minPanePx = 60.f;
        Reactive<Color>                   color     = Color::Rgba(58, 64, 74, 255);
        std::function<void(float, float)> onDrag;   ///< (prevPx, nextPx) every move - live resize
        std::function<void(float, float)> onCommit; ///< (prevPx, nextPx) on release - persist
        Reactive<std::string>             className{};
    };

    [[nodiscard]] LUMORA_API Elem Splitter(SplitterCfg cfg = {});

    // Declarative layout: Tabs/Split/Panel build a DockDecl tree parsed into the model on first mount; later edits
    // mutate the model, not the declaration.

    struct PanelCfg
    {
        std::string id;
        std::string title;
        bool        closable = true;
    };

    struct TabsCfg
    {
    };

    struct SplitCfg
    {
        DockAxis           axis = DockAxis::Horizontal;
        std::vector<float> weights{};
    };

    struct DockCfg
    {
        DockTheme theme{};
    };

    struct PanelDecl
    {
        std::string           id;
        std::string           title;
        bool                  closable = true;
        std::function<Elem()> build;
    };

    struct DockDecl
    {
        bool                   isSplit = false;
        DockAxis               axis = DockAxis::Horizontal;
        std::vector<float>     weights{};
        std::vector<DockDecl>  children{};
        std::vector<PanelDecl> panels{};
    };

    [[nodiscard]] inline PanelDecl Panel(PanelCfg cfg, std::function<Elem()> build)
    {
        return PanelDecl{std::move(cfg.id), std::move(cfg.title), cfg.closable, std::move(build)};
    }

    template <typename... P>
    [[nodiscard]] DockDecl Tabs(TabsCfg, P&&... panels)
    {
        DockDecl d;
        d.isSplit = false;
        (d.panels.push_back(std::forward<P>(panels)), ...);
        return d;
    }

    namespace detail
    {
        /// A Split child may be a DockDecl or a bare PanelDecl (wrapped into a single-panel Tabs leaf).
        inline void AppendSplitChild(DockDecl& parent, DockDecl child)
        {
            parent.children.push_back(std::move(child));
        }
        inline void AppendSplitChild(DockDecl& parent, PanelDecl panel)
        {
            DockDecl leaf;
            leaf.isSplit = false;
            leaf.panels.push_back(std::move(panel));
            parent.children.push_back(std::move(leaf));
        }
    }

    template <typename... C>
    [[nodiscard]] DockDecl Split(SplitCfg cfg, C&&... children)
    {
        DockDecl d;
        d.isSplit = true;
        d.axis = cfg.axis;
        d.weights = std::move(cfg.weights);
        (detail::AppendSplitChild(d, std::forward<C>(children)), ...);
        return d;
    }

    /// Declarative entry: seeds `controller` from `root` on first mount (only while the model is empty, so user edits
    /// survive parent rebuilds), then projects it.
    [[nodiscard]] LUMORA_API Elem Dock(DockController& controller, DockDecl root, DockCfg cfg = {});

    /// Declarative entry with an internally owned controller.
    [[nodiscard]] LUMORA_API Elem Dock(DockCfg cfg, DockDecl root);
}
