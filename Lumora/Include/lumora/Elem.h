#pragma once

#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Input/Event.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace lumora
{
    namespace detail
    {
        /// Erase a class component (anything with Render()) into a ComponentNode; OnMount/OnUnmount/OnEvent are wired only if declared.
        template <typename C>
        Ref<ComponentNode> EraseComponent(C&& component);
    }

    /// Return type of every widget function and Render(): a cheap-to-copy handle over a blueprint Node.
    class Elem
    {
    public:
        Elem() = default;

        Elem(std::nullptr_t) noexcept
        {
        } // NOLINT(*-explicit-constructor)
        Elem(Ref<detail::Node> node) noexcept // NOLINT(*-explicit-constructor)
            : m_node(std::move(node))
        {
        }

        /// Converting constructor from a class component (no-children path; COMP() routes children into `children`).
        template <typename C>
            requires requires(C& c) { c.Render(); }
            && (!std::same_as<std::remove_cvref_t<C>, Elem>)
            && (!std::is_base_of_v<detail::Node, std::remove_cvref_t<C>>)
        constexpr Elem(C&& component)
        { // NOLINT(*-explicit-constructor)
            m_node = detail::EraseComponent(std::forward<C>(component));
        }

        /// Identity key for Each reconciliation.
        Elem& Key(std::uint64_t key)
        {
            if (m_node)
            {
                m_node->SetKey(key);
            }
            return *this;
        }

        [[nodiscard]] explicit operator bool() const noexcept { return m_node != nullptr; }

        [[nodiscard]] detail::Node* RawNode() const noexcept { return m_node.Get(); }
        [[nodiscard]] Ref<detail::Node> Share() const noexcept { return m_node; }

    private:
        Ref<detail::Node> m_node;
    };

    /// A component is any class exposing Render() -> Elem; no base class or virtual dispatch.
    template <typename T>
    concept Component = requires(T& t)
    {
        { t.Render() } -> std::convertible_to<Elem>;
    };

    namespace detail
    {
        template <typename C>
        Ref<ComponentNode> EraseComponent(C&& component)
        {
            using Comp = std::remove_cvref_t<C>;
            auto* instance = new Comp(std::forward<C>(component));

            ErasedComponent erased;
            erased.instance = instance;
            if constexpr (std::is_copy_constructible_v<Comp>)
            {
                erased.clone = [](const void* p) -> void* { return new Comp(*static_cast<const Comp*>(p)); };
            }
            erased.render = [](void* p) -> Elem { return static_cast<Comp*>(p)->Render(); };
            erased.destroy = [](void* p) { delete static_cast<Comp*>(p); };
            if constexpr (requires(Comp& c) { c.OnMount(); })
            {
                erased.mount = [](void* p) { static_cast<Comp*>(p)->OnMount(); };
            }
            if constexpr (requires(Comp& c) { c.OnUnmount(); })
            {
                erased.unmount = [](void* p) { static_cast<Comp*>(p)->OnUnmount(); };
            }
            if constexpr (requires(Comp& c, Event& e) { c.OnEvent(e); })
            {
                erased.event = [](void* p, Event& e) { static_cast<Comp*>(p)->OnEvent(e); };
            }
            return MakeRef<ComponentNode>(ComponentKind, erased);
        }
    }
}
