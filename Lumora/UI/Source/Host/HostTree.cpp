#include <lumora/Host/HostTree.h>

#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Image.h>
#include <lumora/Widgets/Input.h>
#include <lumora/Widgets/Scroll.h>
#include <lumora/Widgets/Text.h>

#include "Host/HostTreeAccess.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ControlRuntime.h"
#include "Runtime/Instantiate.h"

#include <lumora/log/Log.h>

#include <array>
#include <charconv>
#include <limits>
#include <utility>

LUMORA_LOG_CATEGORY("Lumora")

namespace lumora
{
    std::string_view KeyName(const Key key) noexcept
    {
        static constexpr std::string_view kLetters[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                                                        "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};
        static constexpr std::string_view kDigits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        static constexpr std::string_view kFn[] = {"F1",  "F2",  "F3",  "F4",  "F5",  "F6",  "F7",  "F8",
                                                   "F9",  "F10", "F11", "F12", "F13", "F14", "F15", "F16",
                                                   "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"};
        const auto at = [key](Key first) { return static_cast<std::size_t>(key) - static_cast<std::size_t>(first); };
        if (detail::InRange(key, Key::A, Key::Z)) return kLetters[at(Key::A)];
        if (detail::InRange(key, Key::N0, Key::N9)) return kDigits[at(Key::N0)];
        if (detail::InRange(key, Key::Num0, Key::Num9)) return kDigits[at(Key::Num0)];
        if (detail::InRange(key, Key::F1, Key::F24)) return kFn[at(Key::F1)];
        switch (key)
        {
        case Key::Backspace: return "Backspace";
        case Key::Enter: return "Enter";
        case Key::Tab: return "Tab";
        case Key::Escape: return "Escape";
        case Key::Delete: return "Delete";
        case Key::Space: return " ";
        case Key::Insert: return "Insert";
        case Key::PrintScreen: return "PrintScreen";
        case Key::Pause: return "Pause";
        case Key::Left: return "ArrowLeft";
        case Key::Right: return "ArrowRight";
        case Key::Up: return "ArrowUp";
        case Key::Down: return "ArrowDown";
        case Key::Home: return "Home";
        case Key::End: return "End";
        case Key::PageUp: return "PageUp";
        case Key::PageDown: return "PageDown";
        case Key::CapsLock: return "CapsLock";
        case Key::NumLock: return "NumLock";
        case Key::ScrollLock: return "ScrollLock";
        case Key::LeftShift:
        case Key::RightShift: return "Shift";
        case Key::LeftCtrl:
        case Key::RightCtrl: return "Control";
        case Key::LeftAlt:
        case Key::RightAlt: return "Alt";
        case Key::LeftSuper:
        case Key::RightSuper: return "Meta";
        case Key::NumAdd: return "+";
        case Key::NumSub: return "-";
        case Key::NumMul: return "*";
        case Key::NumDiv: return "/";
        case Key::NumDecimal: return ".";
        case Key::NumEnter: return "Enter";
        case Key::Grave: return "`";
        case Key::Minus: return "-";
        case Key::Equal: return "=";
        case Key::LeftBracket: return "[";
        case Key::RightBracket: return "]";
        case Key::Backslash: return "\\";
        case Key::Semicolon: return ";";
        case Key::Apostrophe: return "'";
        case Key::Comma: return ",";
        case Key::Period: return ".";
        case Key::Slash: return "/";
        default: return "Unidentified";
        }
    }

    std::optional<HostTag> ParseHostTag(std::string_view name)
    {
        if (name == "view") return HostTag::View;
        if (name == "text") return HostTag::Text;
        if (name == "button") return HostTag::Button;
        if (name == "input") return HostTag::Input;
        if (name == "image") return HostTag::Image;
        if (name == "scroll") return HostTag::Scroll;
        return std::nullopt;
    }

    std::optional<HostEvent> ParseHostEvent(std::string_view name)
    {
        static constexpr std::string_view kNames[] = {"click", "dblclick", "press",  "release", "move",
                                                      "enter", "leave",    "scroll", "focus",   "blur",
                                                      "keydown", "keyup",  "input",  "submit"};
        for (std::size_t i = 0; i < std::size(kNames); ++i)
        {
            if (kNames[i] == name) return static_cast<HostEvent>(i);
        }
        return std::nullopt;
    }

    namespace
    {
        constexpr std::size_t kEventCount = static_cast<std::size_t>(HostEvent::Submit) + 1;
        using Handlers = std::array<HostHandler, kEventCount>;
        constexpr HostHandle kRoot = 1;

        [[nodiscard]] constexpr std::uint16_t Bit(HostEvent e) noexcept { return std::uint16_t(1u << static_cast<unsigned>(e)); }

        [[nodiscard]] bool IsLeaf(HostTag tag) noexcept
        {
            return tag == HostTag::Text || tag == HostTag::Input || tag == HostTag::Image;
        }

        /// Input routes only its own callbacks; every other tag takes the pointer/focus/key set.
        [[nodiscard]] bool Supports(HostTag tag, HostEvent e) noexcept
        {
            const bool inputOnly = e == HostEvent::Input || e == HostEvent::Submit;
            return tag == HostTag::Input ? inputOnly || e == HostEvent::Blur : !inputOnly;
        }

        [[nodiscard]] std::string ToStr(const HostValue& v)
        {
            if (const auto* s = std::get_if<std::string>(&v)) return *s;
            if (const auto* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
            if (const auto* d = std::get_if<double>(&v))
            {
                char buf[32];
                const auto res = std::to_chars(buf, buf + sizeof(buf), *d);
                return std::string(buf, res.ptr);
            }
            return {};
        }

        [[nodiscard]] bool ToBool(const HostValue& v)
        {
            if (const auto* b = std::get_if<bool>(&v)) return *b;
            if (const auto* d = std::get_if<double>(&v)) return *d != 0.0;
            if (const auto* s = std::get_if<std::string>(&v)) return !s->empty();
            return false;
        }

        struct Node
        {
            HostTag    tag = HostTag::View;
            bool       released = false;
            HostHandle parent = kNoHost;
            HostHandle first = kNoHost;
            HostHandle last = kNoHost;
            HostHandle prev = kNoHost;
            HostHandle next = kNoHost;

            NodeId                 live{};
            std::unique_ptr<Owner> scope;

            Signal<std::string> cls;
            Signal<std::string> style;
            Signal<std::string> text;
            Signal<std::string> value;
            Signal<std::string> placeholder;
            Signal<std::string> src;
            Signal<bool>        disabled{false};
            Signal<ImageFit>    fit{ImageFit::Contain};
            bool                multiline = false;
            ScrollAxis          axis = ScrollAxis::Vertical;

            std::uint16_t             wired = 0; ///< events whose widget Fn was installed at build
            std::shared_ptr<Handlers> handlers = std::make_shared<Handlers>();
        };

        [[nodiscard]] HostEventArgs PointerArgs(const PointerInfo& p)
        {
            HostEventArgs a;
            a.position = p.position;
            a.delta = p.delta;
            a.button = p.button;
            a.mods = p.modifiers;
            return a;
        }

        [[nodiscard]] HostEventArgs KeyArgs(const KeyInfo& k)
        {
            HostEventArgs a;
            a.key = std::string(KeyName(k.key));
            a.mods = k.modifiers;
            return a;
        }

        [[nodiscard]] HostEventArgs TextArgs(std::string text)
        {
            HostEventArgs a;
            a.text = std::move(text);
            return a;
        }
    } // namespace

    struct HostTree::Impl
    {
        AppCtx&     ctx;
        Owner*      rootScope = nullptr;
        std::size_t live = 0;
        // ponytail: handles are never reused, so this grows by one pointer per Create (4 billion creations); a map if that ever bites.
        std::vector<std::unique_ptr<Node>> nodes;

        Impl(AppCtx& c, NodeId root) : ctx(c)
        {
            nodes.resize(kRoot + 1);
            nodes[kRoot] = std::make_unique<Node>();
            nodes[kRoot]->live = root;
            rootScope = ctx.arena.Resolve(root).scope;
        }

        [[nodiscard]] Node* Get(HostHandle h) const noexcept
        {
            return h < nodes.size() ? nodes[h].get() : nullptr;
        }

        /// The handler call path the widget Fns share; copied out so a handler may replace itself.
        static bool Fire(const std::shared_ptr<Handlers>& hs, HostEvent e, const HostEventArgs& args)
        {
            const HostHandler fn = (*hs)[static_cast<std::size_t>(e)];
            return fn && fn(args);
        }

        [[nodiscard]] static InteractProps Wire(const Node& n)
        {
            InteractProps on;
            const std::shared_ptr<Handlers> hs = n.handlers;
            const auto has = [&n](HostEvent e) { return (n.wired & Bit(e)) != 0; };
            const auto pointer = [hs](HostEvent e)
            {
                return Fn<void(PointerInfo)>([hs, e](PointerInfo p) { Fire(hs, e, PointerArgs(p)); });
            };
            const auto plain = [hs](HostEvent e) { return Fn<void()>([hs, e] { Fire(hs, e, {}); }); };
            const auto key = [hs](HostEvent e) { return Fn<bool(KeyInfo)>([hs, e](KeyInfo k) { return Fire(hs, e, KeyArgs(k)); }); };

            if (has(HostEvent::Click)) on.onClick = plain(HostEvent::Click);
            if (has(HostEvent::DoubleClick)) on.onDoubleClick = plain(HostEvent::DoubleClick);
            if (has(HostEvent::Press)) on.onPress = pointer(HostEvent::Press);
            if (has(HostEvent::Release)) on.onRelease = pointer(HostEvent::Release);
            if (has(HostEvent::Move)) on.onMove = pointer(HostEvent::Move);
            if (has(HostEvent::Enter)) on.onEnter = plain(HostEvent::Enter);
            if (has(HostEvent::Leave)) on.onLeave = plain(HostEvent::Leave);
            if (has(HostEvent::Blur)) on.onBlur = plain(HostEvent::Blur);
            if (has(HostEvent::KeyDown)) on.onKeyDown = key(HostEvent::KeyDown);
            if (has(HostEvent::KeyUp)) on.onKeyUp = key(HostEvent::KeyUp);
            if (has(HostEvent::Scroll))
            {
                on.onScroll = [hs](float d)
                {
                    HostEventArgs a;
                    a.delta = {0.f, d};
                    Fire(hs, HostEvent::Scroll, a);
                };
            }
            if (has(HostEvent::Focus))
            {
                on.onFocus = [hs](bool) { Fire(hs, HostEvent::Focus, {}); };
            }
            return on;
        }

        [[nodiscard]] static Elem Blueprint(const Node& n)
        {
            switch (n.tag)
            {
            case HostTag::View:
            case HostTag::Button:
            {
                BoxCfg cfg{.on = Wire(n), .className = n.cls, .inlineCss = n.style};
                if (n.tag == HostTag::Button)
                {
                    cfg.on.focusPolicy = FocusPolicy::Tab;
                    cfg.on.disabled = n.disabled;
                }
                return Box(std::move(cfg));
            }
            case HostTag::Text:
                return Text(n.text, TextCfg{.on = Wire(n), .className = n.cls, .inlineCss = n.style});
            case HostTag::Input:
            {
                const std::shared_ptr<Handlers> hs = n.handlers;
                const auto text = [hs](HostEvent e)
                {
                    return std::function<void(std::string)>([hs, e](std::string s) { Fire(hs, e, TextArgs(std::move(s))); });
                };
                InputCfg cfg{.value = n.value};
                cfg.placeholder = n.placeholder;
                if (n.wired & Bit(HostEvent::Input)) cfg.onChange = text(HostEvent::Input);
                if (n.wired & Bit(HostEvent::Submit)) cfg.onSubmit = text(HostEvent::Submit);
                if (n.wired & Bit(HostEvent::Blur)) cfg.onBlur = text(HostEvent::Blur);
                cfg.multiLine = n.multiline;
                cfg.disabled = n.disabled;
                cfg.className = n.cls;
                cfg.inlineCss = n.style;
                return Input(std::move(cfg));
            }
            case HostTag::Image:
            {
                ImageCfg cfg;
                cfg.src = n.src;
                cfg.fit = n.fit;
                cfg.on = Wire(n);
                cfg.className = n.cls;
                cfg.inlineCss = n.style;
                return Image(std::move(cfg));
            }
            case HostTag::Scroll:
            {
                ScrollCfg cfg{.axis = n.axis, .on = Wire(n), .className = n.cls, .inlineCss = n.style};
                return Scroll(std::move(cfg));
            }
            }
            return Elem{};
        }

        void Build(Node& n, NodeId parentLive)
        {
            for (std::size_t e = 0; e < kEventCount; ++e)
            {
                if ((*n.handlers)[e]) n.wired |= Bit(static_cast<HostEvent>(e));
            }
            if (n.tag == HostTag::Button) n.wired |= Bit(HostEvent::Click);

            // Scoped under the app root, not the parent: a held child outlives a freed parent, and Owner keeps a raw parent pointer.
            MountedBody body = MountBody(ctx, parentLive, [&n] { return Blueprint(n); }, rootScope);
            n.scope = std::move(body.scope);
            n.live = body.root;
        }

        /// Links `h` under a built parent, building it (and its pending children) first if needed.
        void Attach(HostHandle h, NodeId parentLive, NodeId beforeLive)
        {
            Node& n = *Get(h);
            const bool fresh = !n.live.Valid();
            if (fresh) Build(n, parentLive);

            InsertChildBefore(ctx.arena, parentLive, n.live, beforeLive);
            // ponytail: a cross-parent move keeps the subtree's inherited CSS from the first parent until its next restyle.
            LiveNode& ln = ctx.arena.Resolve(n.live);
            ln.paint.valid = false; // the recorded span is relative to the old parent's range
            ln.MarkDirty(Dirty::Layout);
            detail::PropagateDirty(ctx.arena, ln, ln.descendantDirty);
            ctx.arena.Resolve(parentLive).MarkDirty(Dirty::Layout | Dirty::Repaint);

            if (fresh)
            {
                for (HostHandle k = n.first; k != kNoHost; k = Get(k)->next)
                {
                    Attach(k, n.live, NodeId{});
                }
            }
        }

        void Detach(HostHandle h)
        {
            Node& n = *Get(h);
            if (n.parent == kNoHost) return;
            Node& p = *Get(n.parent);
            if (p.live.Valid())
            {
                UnlinkChild(ctx.arena, p.live, n.live);
                ctx.arena.Resolve(n.live).parent = NodeId{};
                ctx.arena.Resolve(p.live).MarkDirty(Dirty::Layout | Dirty::Repaint);
            }
            (n.prev != kNoHost ? Get(n.prev)->next : p.first) = n.next;
            (n.next != kNoHost ? Get(n.next)->prev : p.last) = n.prev;
            n.parent = n.prev = n.next = kNoHost;
        }

        void LinkHost(HostHandle parent, HostHandle child, HostHandle before)
        {
            Node& p = *Get(parent);
            Node& c = *Get(child);
            c.parent = parent;
            c.next = before;
            c.prev = before != kNoHost ? Get(before)->prev : p.last;
            (c.prev != kNoHost ? Get(c.prev)->next : p.first) = child;
            (before != kNoHost ? Get(before)->prev : p.last) = child;
        }

        /// Frees a released, detached handle; children are unlinked first and freed too when released.
        void Free(HostHandle h)
        {
            Node& n = *Get(h);
            while (n.first != kNoHost)
            {
                const HostHandle k = n.first;
                Detach(k);
                if (Get(k)->released) Free(k);
            }
            n.scope.reset();
            nodes[h].reset();
            --live;
        }

        void DoInsert(HostHandle parent, HostHandle child, HostHandle before)
        {
            Node* p = Get(parent);
            Node* c = Get(child);
            if (!p || !c || child == kRoot)
            {
                LOG_WARN("HostTree::Insert: invalid handle (parent {}, child {})", parent, child);
                return;
            }
            if (IsLeaf(p->tag))
            {
                LOG_WARN("HostTree::Insert: a text, input or image node takes no children");
                return;
            }
            for (HostHandle up = parent; up != kNoHost; up = Get(up)->parent)
            {
                if (up == child)
                {
                    LOG_WARN("HostTree::Insert: node {} would become its own descendant", child);
                    return;
                }
            }
            if (before != kNoHost)
            {
                const Node* b = Get(before);
                if (!b || b->parent != parent || before == child)
                {
                    LOG_WARN("HostTree::Insert: `before` {} is not a child of {}", before, parent);
                    return;
                }
            }

            Detach(child);
            LinkHost(parent, child, before);
            if (p->live.Valid())
            {
                Attach(child, p->live, before != kNoHost ? Get(before)->live : NodeId{});
            }
        }

        void DoRemove(HostHandle parent, HostHandle child)
        {
            const Node* c = Get(child);
            if (!c || !Get(parent) || c->parent != parent)
            {
                LOG_WARN("HostTree::Remove: {} is not a child of {}", child, parent);
                return;
            }
            Detach(child);
            if (c->released) Free(child);
        }

        void DoRelease(HostHandle h)
        {
            Node* n = Get(h);
            if (!n || h == kRoot || n->released)
            {
                LOG_WARN("HostTree::Release: invalid handle {}", h);
                return;
            }
            n->released = true;
            if (n->parent == kNoHost) Free(h);
        }

        void DoClear()
        {
            Node& root = *Get(kRoot);
            while (root.first != kNoHost)
            {
                const HostHandle k = root.first;
                Detach(k);
                Get(k)->released = true;
                Free(k);
            }
        }

        /// Runs a structural edit now, or at the next Tick while a dispatch or paint walk is in progress.
        template <typename F>
        void Edit(F&& fn)
        {
            GraphScope ambient(ctx.graph);
            ctx.graph.ApplyStructuralEdit([this, fn = std::forward<F>(fn)]
            {
                GraphScope inner(ctx.graph);
                fn(*this);
            });
        }
    };

    HostTree::HostTree(AppCtx& ctx, NodeId root) : m_impl(std::make_unique<Impl>(ctx, root)) {}

    HostTree::~HostTree()
    {
        GraphScope ambient(m_impl->ctx.graph);
        for (HostHandle h = kRoot + 1; h < m_impl->nodes.size(); ++h)
        {
            if (m_impl->nodes[h]) m_impl->Detach(h);
        }
        for (HostHandle h = kRoot + 1; h < m_impl->nodes.size(); ++h)
        {
            if (m_impl->nodes[h]) m_impl->nodes[h]->scope.reset();
        }
    }

    HostHandle HostTree::Root() const noexcept { return kRoot; }

    HostHandle HostTree::Create(const HostTag tag)
    {
        if (m_impl->nodes.size() >= std::numeric_limits<HostHandle>::max())
        {
            LOG_ERROR("HostTree::Create: handle space exhausted");
            return kNoHost;
        }
        auto node = std::make_unique<Node>();
        node->tag = tag;
        m_impl->nodes.push_back(std::move(node));
        ++m_impl->live;
        return static_cast<HostHandle>(m_impl->nodes.size() - 1);
    }

    void HostTree::Insert(HostHandle parent, HostHandle child, HostHandle before)
    {
        m_impl->Edit([parent, child, before](Impl& m) { m.DoInsert(parent, child, before); });
    }

    void HostTree::Remove(HostHandle parent, HostHandle child)
    {
        m_impl->Edit([parent, child](Impl& m) { m.DoRemove(parent, child); });
    }

    void HostTree::Release(HostHandle node)
    {
        m_impl->Edit([node](Impl& m) { m.DoRelease(node); });
    }

    void HostTree::Clear()
    {
        m_impl->Edit([](Impl& m) { m.DoClear(); });
    }

    bool HostTree::SetProp(HostHandle h, std::string_view name, const HostValue& value)
    {
        Node* n = m_impl->Get(h);
        if (!n || h == kRoot)
        {
            LOG_WARN("HostTree::SetProp: invalid handle {}", h);
            return true;
        }
        GraphScope ambient(m_impl->ctx.graph);
        const HostTag tag = n->tag;
        const bool built = n->live.Valid();

        if (name == "class") n->cls = ToStr(value);
        else if (name == "style") n->style = ToStr(value);
        else if (name == "text" && tag == HostTag::Text) n->text = ToStr(value);
        else if (name == "value" && tag == HostTag::Input) n->value = ToStr(value);
        else if (name == "placeholder" && tag == HostTag::Input) n->placeholder = ToStr(value);
        else if (name == "disabled" && (tag == HostTag::Button || tag == HostTag::Input)) n->disabled = ToBool(value);
        else if (name == "src" && tag == HostTag::Image) n->src = ToStr(value);
        else if (name == "fit" && tag == HostTag::Image)
        {
            const std::string s = ToStr(value);
            if (s.empty() || s == "contain") n->fit = ImageFit::Contain;
            else if (s == "fill") n->fit = ImageFit::Fill;
            else if (s == "cover") n->fit = ImageFit::Cover;
            else if (s == "none") n->fit = ImageFit::None;
            else LOG_WARN("HostTree::SetProp: unknown fit '{}'", s);
        }
        else if ((name == "multiline" && tag == HostTag::Input) || (name == "axis" && tag == HostTag::Scroll))
        {
            if (built)
            {
                LOG_WARN("HostTree::SetProp: '{}' is creation-only; ignored after the node is inserted", name);
                return true;
            }
            if (tag == HostTag::Input)
            {
                n->multiline = ToBool(value);
                return true;
            }
            const std::string s = ToStr(value);
            if (s.empty() || s == "vertical") n->axis = ScrollAxis::Vertical;
            else if (s == "horizontal") n->axis = ScrollAxis::Horizontal;
            else if (s == "both") n->axis = ScrollAxis::Both;
            else LOG_WARN("HostTree::SetProp: unknown axis '{}'", s);
        }
        else
        {
            return false;
        }
        return true;
    }

    void HostTree::SetHandler(HostHandle h, HostEvent event, HostHandler fn)
    {
        Node* n = m_impl->Get(h);
        if (!n || h == kRoot)
        {
            LOG_WARN("HostTree::SetHandler: invalid handle {}", h);
            return;
        }
        if (!Supports(n->tag, event))
        {
            LOG_WARN("HostTree::SetHandler: event {} is not available on this tag", static_cast<int>(event));
            return;
        }
        if (fn && n->live.Valid() && (n->wired & Bit(event)) == 0)
        {
            LOG_WARN("HostTree::SetHandler: event {} added after the node was inserted; ignored", static_cast<int>(event));
            return;
        }
        (*n->handlers)[static_cast<std::size_t>(event)] = std::move(fn);
    }

    std::size_t HostTree::HandleCount() const noexcept { return m_impl->live; }

    std::vector<std::string> HostTree::TextContents() const
    {
        std::vector<std::string> out;
        NodeArena& arena = m_impl->ctx.arena;
        const auto walk = [&](const auto& self, NodeId id) -> void
        {
            for (NodeId c = arena.Resolve(id).firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
            {
                if (arena.Resolve(c).kind == LiveKind::Text) out.push_back(arena.PayloadOf<TextPayload>(c).text);
                self(self, c);
            }
        };
        walk(walk, m_impl->Get(kRoot)->live);
        return out;
    }

    std::uint32_t HostTreeTestAccess::LiveNodes(const HostTree& tree) { return tree.m_impl->ctx.arena.LiveCount(); }

    Rect HostTreeTestAccess::Solved(const HostTree& tree, HostHandle node)
    {
        const Node* n = tree.m_impl->Get(node);
        return n && n->live.Valid() ? tree.m_impl->ctx.arena.Resolve(n->live).solved : Rect{};
    }
} // namespace lumora
