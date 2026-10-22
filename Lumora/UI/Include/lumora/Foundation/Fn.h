#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

namespace lumora
{
    namespace detail
    {
        template <bool Copyable, typename Sig>
        class BasicFn;

        template <bool Copyable, typename R, typename... Args>
        class BasicFn<Copyable, R(Args...)>
        {
            struct Base
            {
                virtual ~Base() = default;
                virtual R Call(Args... args) = 0;
                [[nodiscard]] virtual Base* Clone() const = 0;
            };

            template <typename F>
            struct Impl final : Base
            {
                template <typename G>
                explicit Impl(G&& g) : fn(std::forward<G>(g)) {}
                R Call(Args... args) override { return fn(std::forward<Args>(args)...); }
                [[nodiscard]] Base* Clone() const override
                {
                    if constexpr (std::is_copy_constructible_v<F>)
                    {
                        return new Impl(fn);
                    }
                    else
                    {
                        return nullptr; // unreachable: only a Copyable BasicFn copies, and it accepts only copyable targets
                    }
                }
                F fn;
            };

        public:
            BasicFn() noexcept = default;
            BasicFn(std::nullptr_t) noexcept {} // NOLINT(*-explicit-constructor)

            template <typename F>
                requires(!std::is_same_v<std::remove_cvref_t<F>, BasicFn> &&
                         std::is_invocable_r_v<R, std::decay_t<F>&, Args...> &&
                         (!Copyable || std::is_copy_constructible_v<std::decay_t<F>>))
            BasicFn(F&& f) // NOLINT(*-explicit-constructor)
            {
                using D = std::decay_t<F>;
                if constexpr (std::is_pointer_v<D> || std::is_member_pointer_v<D> ||
                              requires(const D& d) { d == nullptr; })
                {
                    if (f == nullptr)
                    {
                        return; // an empty std::function or null pointer stays empty
                    }
                }
                m_target = new Impl<D>(std::forward<F>(f));
            }

            BasicFn(const BasicFn& other)
                requires Copyable
                : m_target(other.m_target ? other.m_target->Clone() : nullptr)
            {
            }
            BasicFn(BasicFn&& other) noexcept : m_target(std::exchange(other.m_target, nullptr)) {}

            BasicFn& operator=(const BasicFn& other)
                requires Copyable
            {
                if (this != &other)
                {
                    BasicFn copy(other);
                    std::swap(m_target, copy.m_target);
                }
                return *this;
            }

            BasicFn& operator=(BasicFn&& other) noexcept
            {
                std::swap(m_target, other.m_target);
                return *this;
            }

            ~BasicFn() { delete m_target; }

            R operator()(Args... args) const { return m_target->Call(std::forward<Args>(args)...); }

            [[nodiscard]] explicit operator bool() const noexcept { return m_target != nullptr; }
            [[nodiscard]] friend bool operator==(const BasicFn& f, std::nullptr_t) noexcept { return f.m_target == nullptr; }

        private:
            Base* m_target = nullptr;
        };
    } // namespace detail

    /// A callable one pointer wide: empty costs 8 bytes, a set callable lives on the heap. Used where most slots stay unset or
    /// where one exists per node (interaction handlers, reactive computations, cleanups), so std::function's inline buffer
    /// (64 bytes on MSVC, 32 on libc++) would be paid for nothing. Copy it as Fn, never into a std::function: an empty Fn wraps
    /// into a non-empty std::function that crashes when called.
    template <typename Sig>
    using Fn = detail::BasicFn<true, Sig>; ///< copyable; copying clones the target, as std::function does

    template <typename Sig>
    using MoveFn = detail::BasicFn<false, Sig>; ///< move-only, so it also holds move-only callables
} // namespace lumora
