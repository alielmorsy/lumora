#pragma once

#include <lumora/Elem.h>
#include <lumora/Input/Event.h>
#include <lumora/Widgets/Children.h>

#include <type_traits>
#include <utility>

namespace lumora
{

    template <typename T>
    concept HasOnMount = requires(T& t) { t.OnMount(); };

    template <typename T>
    concept HasOnUnmount = requires(T& t) { t.OnUnmount(); };

    /// Input hook, called on the capture pass (root to target) and the bubble pass (target to root); check `e.Phase` and call
    /// `e.Consume()` to take first refusal on capture or handle a bubble fallback.
    template <typename T>
    concept HasOnEvent = requires(T& t, Event& e) { t.OnEvent(e); };

    /// Opt into caller-supplied children by declaring a public `Children children;` member and splicing it in Render();
    /// COMP() fills it before Render().
    template <typename T>
    concept HasChildren = requires(T& t, Children c) { t.children = std::move(c); };

    namespace detail
    {
        /// Erase a class component and move `kids` into its `children` slot (if it has one).
        template <typename C>
        Elem MakeComponentElem(C&& component, Children kids)
        {
            Ref<ComponentNode> node = EraseComponent(std::forward<C>(component));
            using Comp = std::remove_cvref_t<C>;
            if constexpr (HasChildren<Comp>)
            {
                static_cast<Comp*>(node->erased.instance)->children = std::move(kids);
            }
            return {std::move(node)};
        }

        /// Backing call for COMP(): the macro keeps `Type` and the children apart so the compiler parses the braced props
        /// (designated-initializer commas are safe).
        template <typename C, typename... Kids>
            requires Component<std::remove_cvref_t<C>>
        Elem BuildComp(C component, Kids&&... kids)
        {
            static_assert(HasChildren<std::remove_cvref_t<C>> || sizeof...(Kids) == 0,
                          "COMP: this component has no `Children children;` member to receive children");
            return MakeComponentElem(std::move(component), Children{std::forward<Kids>(kids)...});
        }
    }
}

/// Render a class component inline with caller-supplied children: `COMP(Panel, {.tint = Blue}, Text("a"), Button({...}))`.
/// The children land in the component's `Children children;` member. Needs the conforming preprocessor on MSVC (/Zc:preprocessor).
#define COMP(Type, ...) ::lumora::detail::BuildComp<Type>(__VA_ARGS__)
