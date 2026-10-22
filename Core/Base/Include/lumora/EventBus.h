#pragma once

#include "Ref.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace lumora
{
    namespace detail
    {
        class EventBusBase
        {
        public:
            virtual ~EventBusBase() = default;
            virtual void Remove(uint64_t id) noexcept = 0;
        };

        // Shared link between a bus and its Subscriptions; the bus nulls `bus` on destruction so an outliving Subscription becomes a no-op.
        class BusLink : public RefCounted
        {
        public:
            EventBusBase* bus = nullptr;
        };
    }

    // Move-only RAII handle from Subscribe. Discarding it unsubscribes immediately; store it as long as the handler should live.
    class Subscription
    {
    public:
        Subscription() noexcept = default;

        Subscription(Ref<detail::BusLink> link, uint64_t id) noexcept
            : _link(std::move(link)), _id(id)
        {
        }

        Subscription(const Subscription&)            = delete;
        Subscription& operator=(const Subscription&) = delete;

        Subscription(Subscription&& other) noexcept
            : _link(std::move(other._link)), _id(other._id)
        {
            other._id = 0;
        }

        Subscription& operator=(Subscription&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                _link     = std::move(other._link);
                _id       = other._id;
                other._id = 0;
            }
            return *this;
        }

        ~Subscription() { Reset(); }

        void Reset() noexcept
        {
            if (_link)
            {
                if (_link->bus) _link->bus->Remove(_id);
                _link = nullptr;
                _id   = 0;
            }
        }

        [[nodiscard]] bool Active() const noexcept
        {
            return _link && _link->bus != nullptr;
        }

    private:
        Ref<detail::BusLink> _link;
        uint64_t             _id = 0;
    };

    template <typename Event>
    class EventBus final : public detail::EventBusBase
    {
    public:
        using Handler = std::function<void(const Event&)>;

        EventBus() : _link(MakeRef<detail::BusLink>()) { _link->bus = this; }

        ~EventBus() override { _link->bus = nullptr; }

        EventBus(const EventBus&)            = delete;
        EventBus& operator=(const EventBus&) = delete;

        [[nodiscard]] Subscription Subscribe(Handler handler)
        {
            const uint64_t id = ++_nextId;
            _slots.push_back(Slot{ id, std::move(handler) });
            return Subscription(_link, id);
        }

        // Synchronous dispatch. Safe to subscribe, unsubscribe, or re-enter from
        // inside a handler: newly added handlers do not fire this round, removed
        // handlers are skipped and compacted once the top-level dispatch unwinds.
        void Publish(const Event& event)
        {
            const bool reentrant = _dispatching;
            _dispatching = true;

            const std::size_t count = _slots.size();
            for (std::size_t i = 0; i < count && i < _slots.size(); ++i)
            {
                if (_slots[i].handler) _slots[i].handler(event);
            }

            _dispatching = reentrant;
            if (!_dispatching) Compact();
        }

        void Remove(uint64_t id) noexcept override
        {
            for (Slot& slot : _slots)
            {
                if (slot.id == id)
                {
                    slot.handler = nullptr;
                    _dirty       = true;
                    break;
                }
            }
            if (!_dispatching) Compact();
        }

        [[nodiscard]] bool Empty() const noexcept { return _slots.empty(); }

    private:
        struct Slot
        {
            uint64_t id;
            Handler  handler;
        };

        void Compact() noexcept
        {
            if (!_dirty) return;
            std::erase_if(_slots, [](const Slot& s) { return !s.handler; });
            _dirty = false;
        }

        std::vector<Slot>    _slots;
        Ref<detail::BusLink> _link;
        uint64_t             _nextId      = 0;
        bool                 _dispatching = false;
        bool                 _dirty       = false;
    };
}
