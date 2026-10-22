#pragma once

#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>

#include <concepts>
#include <initializer_list>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

namespace lumora
{
    namespace detail
    {
        template <typename R>
        concept ElemRange = requires(R&& r)
        {
            { *std::begin(r) } -> std::convertible_to<Elem>;
            std::end(r);
        };
    }

    /// Eager, caller-owned content spliced into a custom component (values pass eagerly; only closures pass laziness: that is
    /// Slot). Constructible from an Elem, a braced list or a range of Elem.
    class Children
    {
    public:
        Children() = default;

        Children(Elem elem)
        { // NOLINT(*-explicit-constructor)
            if (elem)
            {
                m_items.push_back(std::move(elem));
            }
        }

        Children(std::initializer_list<Elem> elems)
        { // NOLINT(*-explicit-constructor)
            m_items.reserve(elems.size());
            for (const Elem& elem : elems)
            {
                if (elem)
                {
                    m_items.push_back(elem);
                }
            }
        }

        template <typename R>
            requires (!std::same_as<std::remove_cvref_t<R>, Children>) && detail::ElemRange<R>
        Children(R&& range)
        { // NOLINT(*-explicit-constructor)
            for (const Elem& elem : range)
            {
                if (elem)
                {
                    m_items.push_back(elem);
                }
            }
        }

        [[nodiscard]] const std::vector<Elem>& Items() const noexcept { return m_items; }

    private:
        std::vector<Elem> m_items;
    };

    namespace detail
    {
        /// Append one variadic child (Elem, class component, Children bundle or range of Elem), flattened into the parent's child list.
        template <typename K>
        void AppendKid(const Ref<Node>& parent, K&& kid)
        {
            using T = std::remove_cvref_t<K>;
            if constexpr (std::same_as<T, Children>)
            {
                for (const Elem& elem : kid.Items())
                {
                    parent->AddChild(elem.Share());
                }
            }
            else if constexpr (std::convertible_to<K, Elem>)
            {
                Elem elem(std::forward<K>(kid));
                if (elem)
                {
                    parent->AddChild(elem.Share());
                }
            }
            else
            {
                static_assert(ElemRange<K>, "widget child must be an Elem, a component, Children, or a range of Elem");
                for (const Elem& elem : kid)
                {
                    if (elem)
                    {
                        parent->AddChild(elem.Share());
                    }
                }
            }
        }

        template <typename... Ks>
        void AppendKids(const Ref<Node>& parent, Ks&&... kids)
        {
            (AppendKid(parent, std::forward<Ks>(kids)), ...);
        }
    }
}
