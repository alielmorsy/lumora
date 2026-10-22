#pragma once

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace lumora {

/// Type-erased pool face so the arena can free a node's payload knowing only its PayloadRef
/// (pool kind + index), without naming the payload type.
class IPayloadPool {
public:
    virtual ~IPayloadPool() = default;
    virtual void Free(std::uint32_t index) = 0;
};

/// Per-kind, vector-backed payload storage with slot recycling. The vector relocates, so resolve through Get() on each access: never cache a reference across an Alloc.
template<typename T>
class PayloadPool final : public IPayloadPool {
public:
    template<typename... Args>
    [[nodiscard]] std::uint32_t Alloc(Args&&... args) {
        if (!m_free.empty()) {
            const std::uint32_t index = m_free.back();
            m_free.pop_back();
            m_slots[index].value = T(std::forward<Args>(args)...);
            m_slots[index].live  = true;
            return index;
        }
        const auto index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.push_back(Slot{T(std::forward<Args>(args)...), true});
        return index;
    }

    void Free(std::uint32_t index) override {
        assert(index < m_slots.size() && m_slots[index].live);
        m_slots[index].value = T{}; // release any held resources eagerly
        m_slots[index].live  = false;
        m_free.push_back(index);
    }

    [[nodiscard]] T& Get(std::uint32_t index) {
        assert(index < m_slots.size() && m_slots[index].live);
        return m_slots[index].value;
    }

private:
    struct Slot {
        T    value;
        bool live = false;
    };

    std::vector<Slot>          m_slots;
    std::vector<std::uint32_t> m_free;
};

} // namespace lumora
