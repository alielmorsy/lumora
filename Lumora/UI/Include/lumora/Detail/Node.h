#pragma once

#include <lumora/Ref.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace lumora {

class Elem;
struct Event;

}

namespace lumora::detail {

/// Opaque live-kind id stamped on a blueprint by private builder code; the dispatch index for the instantiation walker.
using KindId = std::uint16_t;

inline constexpr KindId InvalidKind   = 0;
inline constexpr KindId ComponentKind = 1; ///< must equal static_cast<KindId>(LiveKind::Component)

/// Transient blueprint node built by a widget function or Render(); dies once the live tree is instantiated.
class Node : public RefCounted {
public:
    explicit Node(KindId kind) noexcept : m_kind(kind) {}

    [[nodiscard]] KindId Kind() const noexcept { return m_kind; }

    [[nodiscard]] const std::vector<Ref<Node>>& Children() const noexcept { return m_children; }
    void AddChild(Ref<Node> child) {
        if (child) {
            m_children.push_back(std::move(child));
        }
    }

    [[nodiscard]] bool          HasKey() const noexcept { return m_hasKey; }
    [[nodiscard]] std::uint64_t Key() const noexcept { return m_key; }
    void SetKey(std::uint64_t key) noexcept {
        m_key = key;
        m_hasKey = true;
    }

protected:
    KindId                 m_kind;
    std::vector<Ref<Node>> m_children;
    std::uint64_t          m_key = 0;
    bool                   m_hasKey = false;
};

/// Blueprint node carrying a widget's config inline; the descriptor static_casts back to TypedNode<Cfg>
/// (safe: only that builder stamps the kind).
template<typename Cfg>
class TypedNode final : public Node {
public:
    TypedNode(KindId kind, Cfg config) : Node(kind), config(std::move(config)) {}

    Cfg config;
};

/// Class component erased without std::any/RTTI; the heap instance is the blueprint's PROTOTYPE.
/// Blueprints replay (Show/Accordion bodies), so a copy-constructible class sets `clone` and each mount copies it.
/// A non-copyable class has no `clone`: the one mount takes the instance; a second mount is refused with an error.
struct ErasedComponent {
    void* instance = nullptr;
    void* (*clone)(const void*)  = nullptr;
    Elem (*render)(void*)        = nullptr;
    void (*destroy)(void*)       = nullptr;
    void (*mount)(void*)         = nullptr;
    void (*unmount)(void*)       = nullptr;
    void (*event)(void*, Event&) = nullptr;
};

class ComponentNode final : public Node {
public:
    ComponentNode(KindId kind, ErasedComponent erased) : Node(kind), erased(erased) {}

    ~ComponentNode() override {
        if (erased.instance && erased.destroy) {
            erased.destroy(erased.instance);
        }
    }

    ErasedComponent erased;
};

}
