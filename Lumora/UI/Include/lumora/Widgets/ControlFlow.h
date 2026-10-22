#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace lumora {

// Control flow is explicit because the tree is built once: conditionals and lists are primitives the runtime reconciles,
// not `if`/`for` in the body.

/// Evaluated once at build: contributes `elem` when `condition` holds. No scope, no reactivity (feature flags).
[[nodiscard]] inline Elem When(bool condition, Elem elem) {
    return condition ? std::move(elem) : Elem{};
}

namespace detail {

/// A reactive conditional branch; `build`/`fallback` run under a fresh scope on mount and are disposed on unmount (`fallback` may be empty).
struct ShowConfig {
    Reactive<bool>        cond;
    std::function<Elem()> build;
    std::function<Elem()> fallback;
};
[[nodiscard]] LUMORA_API Ref<Node> BuildShow(ShowConfig cfg);

/// One reconciled row: a 64-bit identity key and a thunk that builds the row's Elem on first mount (captures its item by value).
struct EachRow {
    std::uint64_t         key;
    std::function<Elem()> build;
};

/// Reads the backing signal and projects the current items into keyed rows; re-invoked on every source change.
struct EachConfig {
    std::function<std::vector<EachRow>()> snapshot;
};
[[nodiscard]] LUMORA_API Ref<Node> BuildEach(EachConfig cfg);

/// Reduce a projected key of any hashable/integral type to the runtime's 64-bit identity.
template<typename K>
[[nodiscard]] std::uint64_t HashKey(const K& key) {
    if constexpr (std::is_integral_v<K> || std::is_enum_v<K>) {
        return static_cast<std::uint64_t>(key);
    } else {
        return static_cast<std::uint64_t>(std::hash<K>{}(key));
    }
}

/// Fold one value into a running key: strings hash by content (not pointer), ranges fold per element, else HashKey.
template<typename K>
[[nodiscard]] std::uint64_t FoldKey(std::uint64_t hash, const K& value) {
    if constexpr (std::is_convertible_v<K, std::string_view>) {
        hash = (hash ^ HashKey(std::string_view(value))) * 1099511628211ull;
    } else if constexpr (requires(const K& r) { std::begin(r); std::end(r); }) {
        for (const auto& element : value) hash = FoldKey(hash, element);
    } else {
        hash = (hash ^ HashKey(value)) * 1099511628211ull;
    }
    return hash;
}

/// Hash `item` to its identity key via a member object pointer, member function pointer or callable.
template<typename T, typename Proj>
[[nodiscard]] std::uint64_t ProjectKey(const T& item, Proj& proj) {
    if constexpr (std::is_member_object_pointer_v<Proj>) {
        return HashKey(item.*proj);
    } else if constexpr (std::is_member_function_pointer_v<Proj>) {
        return HashKey((item.*proj)());
    } else {
        return HashKey(proj(item));
    }
}

/// Erased, value-comparable argument bundle for Memo; type identity is a per-instantiation tag address (no RTTI).
struct MemoArgsBase : RefCounted {
    explicit MemoArgsBase(const void* typeTag) noexcept : tag(typeTag) {}

    [[nodiscard]] bool Equals(const MemoArgsBase& other) const noexcept {
        return tag == other.tag && EqualsSameType(other);
    }

protected:
    [[nodiscard]] virtual bool EqualsSameType(const MemoArgsBase& other) const noexcept = 0;

private:
    const void* tag;
};

template<typename... Args>
struct MemoArgs final : MemoArgsBase {
    explicit MemoArgs(std::tuple<Args...> v) : MemoArgsBase(&s_tag), values(std::move(v)) {}

    std::tuple<Args...> values;

protected:
    [[nodiscard]] bool EqualsSameType(const MemoArgsBase& other) const noexcept override {
        // Tags matched, so the cast targets exactly this instantiation.
        return values == static_cast<const MemoArgs&>(other).values;
    }

private:
    inline static const char s_tag = 0;
};

struct MemoConfig {
    Ref<MemoArgsBase>     args;
    std::function<Elem()> build;
};
[[nodiscard]] LUMORA_API Ref<Node> BuildMemo(MemoConfig cfg);

}

/// Mount the body from `build` while `cond` holds; disposes its scope (cleanups, subtree) when it flips off. Do not read a
/// bare signal inside the closure: read it through a child widget's Reactive prop (debug builds warn).
template<typename Builder>
requires std::invocable<Builder> && std::convertible_to<std::invoke_result_t<Builder>, Elem>
[[nodiscard]] Elem Show(Reactive<bool> cond, Builder build) {
    detail::ShowConfig cfg;
    cfg.cond  = std::move(cond);
    cfg.build = [build = std::move(build)]() -> Elem { return build(); };
    return Elem(detail::BuildShow(std::move(cfg)));
}

/// Show with a fallback body built while `cond` is false.
template<typename Builder, typename Fallback>
requires std::invocable<Builder> && std::convertible_to<std::invoke_result_t<Builder>, Elem>
      && std::invocable<Fallback> && std::convertible_to<std::invoke_result_t<Fallback>, Elem>
[[nodiscard]] Elem Show(Reactive<bool> cond, Builder build, Fallback fallback) {
    detail::ShowConfig cfg;
    cfg.cond     = std::move(cond);
    cfg.build    = [build = std::move(build)]() -> Elem { return build(); };
    cfg.fallback = [fallback = std::move(fallback)]() -> Elem { return fallback(); };
    return Elem(detail::BuildShow(std::move(cfg)));
}

/// Static range expanded once at build into a vector<Elem>. The row builder takes `(const T&)` or `(const T&, int index)`.
template<typename Range, typename RowBuilder>
requires requires(const Range& r) { std::begin(r); std::end(r); }
[[nodiscard]] std::vector<Elem> Each(const Range& items, RowBuilder rowBuilder) {
    std::vector<Elem> out;
    int index = 0;
    for (const auto& item : items) {
        if constexpr (std::invocable<RowBuilder&, decltype(item), int>) {
            out.push_back(rowBuilder(item, index));
        } else {
            out.push_back(rowBuilder(item));
        }
        ++index;
    }
    return out;
}

/// Fold several values into one `Each` identity key. A row whose key survives is reused WITHOUT re-running its builder, so
/// anything it captured by value is frozen: a row that bakes a value into text, class, presence or a closure must key on it.
template<typename... Ks>
[[nodiscard]] std::uint64_t RowKey(const Ks&... keys) {
    std::uint64_t hash = 1469598103934665603ull;
    ((hash = detail::FoldKey(hash, keys)), ...);
    return hash;
}

/// Reactive + keyed: reconciled when `items` changes. Rows whose key survives are reused (scope, signals, focus) and their
/// builder is NOT re-run; new keys instantiate, dead keys dispose. `items` is a Signal or Computed of a vector; `keyProj`
/// is a member pointer or callable yielding a hashable identity. See `RowKey`.
template<typename Source, typename Proj, typename RowBuilder,
         typename T = typename std::decay_t<std::invoke_result_t<Source&>>::value_type>
requires (std::is_member_pointer_v<Proj> || std::invocable<Proj&, const T&>)
[[nodiscard]] Elem Each(Source items, Proj keyProj, RowBuilder rowBuilder) {
    detail::EachConfig cfg;
    cfg.snapshot = [items = std::move(items), keyProj = std::move(keyProj),
                    rowBuilder = std::move(rowBuilder)]() mutable -> std::vector<detail::EachRow> {
        const std::vector<T>& vec = items(); // subscribes the controlling effect to the source
        std::vector<detail::EachRow> rows;
        rows.reserve(vec.size());
        int index = 0;
        for (const T& item : vec) {
            const std::uint64_t key = detail::ProjectKey(item, keyProj);
            rows.push_back(detail::EachRow{key, [item, rowBuilder, index]() mutable -> Elem {
                if constexpr (std::invocable<RowBuilder&, const T&, int>) {
                    return rowBuilder(item, index);
                } else {
                    return rowBuilder(item);
                }
            }});
            ++index;
        }
        return rows;
    };
    return Elem(detail::BuildEach(std::move(cfg)));
}

/// Reactive + index-keyed (no projection): rows are reused by POSITION, so per-row state follows position, not identity;
/// prefer the keyed form for stateful rows.
template<typename Source, typename RowBuilder,
         typename T = typename std::decay_t<std::invoke_result_t<Source&>>::value_type>
requires std::invocable<RowBuilder&, const T&> || std::invocable<RowBuilder&, const T&, int>
[[nodiscard]] Elem Each(Source items, RowBuilder rowBuilder) {
    detail::EachConfig cfg;
    cfg.snapshot = [items = std::move(items), rowBuilder = std::move(rowBuilder)]() mutable -> std::vector<detail::EachRow> {
        const std::vector<T>& vec = items();
        std::vector<detail::EachRow> rows;
        rows.reserve(vec.size());
        int index = 0;
        for (const T& item : vec) {
            rows.push_back(detail::EachRow{static_cast<std::uint64_t>(index),
                                           [item, rowBuilder, index]() mutable -> Elem {
                if constexpr (std::invocable<RowBuilder&, const T&, int>) {
                    return rowBuilder(item, index);
                } else {
                    return rowBuilder(item);
                }
            }});
            ++index;
        }
        return rows;
    };
    return Elem(detail::BuildEach(std::move(cfg)));
}

/// Build `component(args...)` once and rebuild only when an arg changes by `==`. For pure components that capture no
/// signals. Re-evaluates only when a parent Show/Each re-instantiates it, not standalone.
template<typename Component, typename... Args>
requires std::invocable<Component, const Args&...>
      && std::convertible_to<std::invoke_result_t<Component, const Args&...>, Elem>
      && (std::equality_comparable<Args> && ...)
[[nodiscard]] Elem Memo(Component component, Args... args) {
    auto values = std::make_tuple(std::move(args)...);
    detail::MemoConfig cfg;
    cfg.args  = MakeRef<detail::MemoArgs<Args...>>(values);
    cfg.build = [component = std::move(component), values = std::move(values)]() -> Elem {
        return std::apply([&component](const Args&... a) -> Elem { return component(a...); }, values);
    };
    return Elem(detail::BuildMemo(std::move(cfg)));
}

}
