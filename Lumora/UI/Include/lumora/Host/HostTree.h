#pragma once

#include <lumora/Api.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Input/Event.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lumora {

struct AppCtx;
struct NodeId;

enum class HostTag : std::uint8_t { View, Text, Button, Input, Image, Scroll };
enum class HostEvent : std::uint8_t { Click, DoubleClick, Press, Release, Move, Enter, Leave, Scroll, Focus, Blur, KeyDown, KeyUp, Input, Submit };

/// "view", "text", "button", "input", "image", "scroll".
[[nodiscard]] LUMORA_API std::optional<HostTag> ParseHostTag(std::string_view name);
/// "click", "dblclick", "press", "release", "move", "enter", "leave", "scroll", "focus", "blur", "keydown", "keyup", "input", "submit".
[[nodiscard]] LUMORA_API std::optional<HostEvent> ParseHostEvent(std::string_view name);

using HostValue = std::variant<std::monostate, bool, double, std::string>;

struct HostEventArgs {
    Vec2        position{};
    Vec2        delta{};
    int         button = 0;
    std::string key;
    std::string text;
    KeyMods     mods{};
};

/// True = consumed (only key events read it).
using HostHandler = std::function<bool(const HostEventArgs&)>;
using HostHandle  = std::uint32_t;
inline constexpr HostHandle kNoHost = 0;

/// Imperative node tree over one MountedApp's root, for script renderers. A created node records its tag and props and is built
/// into live nodes when it first lands under the root's tree. A handle is freed once it is both released and detached.
/// Structural calls made during a dispatch apply at the next Tick, like Show/Each splices.
class LUMORA_API HostTree {
public:
    ~HostTree();
    HostTree(const HostTree&)            = delete;
    HostTree& operator=(const HostTree&) = delete;

    [[nodiscard]] HostHandle Root() const noexcept;
    [[nodiscard]] HostHandle Create(HostTag tag);
    /// Links `child` before `before` (append when kNoHost); moves it if already attached.
    void Insert(HostHandle parent, HostHandle child, HostHandle before = kNoHost);
    void Remove(HostHandle parent, HostHandle child);
    /// False: unknown prop for the tag. `monostate` resets to the widget default.
    bool SetProp(HostHandle node, std::string_view name, const HostValue& value);
    /// An empty fn clears.
    void SetHandler(HostHandle node, HostEvent event, HostHandler fn);
    /// The script dropped its reference.
    void Release(HostHandle node);
    /// Detach and release every child of Root().
    void Clear();
    /// Handles not yet freed, Root excluded.
    [[nodiscard]] std::size_t HandleCount() const noexcept;
    /// Text contents under Root, tree order.
    [[nodiscard]] std::vector<std::string> TextContents() const;

private:
    friend class MountedApp;
    friend struct HostTreeTestAccess;

    HostTree(AppCtx& ctx, NodeId root);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
