#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

namespace lumora
{
    class RefCounted
    {
    public:
        RefCounted() : _refCount(0) {}
        virtual ~RefCounted() = default;

        RefCounted(const RefCounted&) : _refCount(0) {}
        RefCounted& operator=(const RefCounted&) { return *this; }

        RefCounted(RefCounted&&) noexcept : _refCount(0) {}
        RefCounted& operator=(RefCounted&&) noexcept { return *this; }

        virtual void AddRef() const
        {
            _refCount.fetch_add(1, std::memory_order_relaxed);
        }

        virtual void Release() const
        {
            if (_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                OnLastRef();
            }
        }

        void RemoveRef() const { Release(); }

        /// Called when the last reference goes; overridable so an owner can drop its non-owning index entry or defer
        /// destruction to a particular thread.
        virtual void OnLastRef() const { delete this; }

        [[nodiscard]] uint32_t GetRefCount() const
        {
            return _refCount.load(std::memory_order_relaxed);
        }

    private:
        mutable std::atomic<uint32_t> _refCount;
    };

    template <typename T>
    class Ref
    {
    public:
        Ref() noexcept : _ptr(nullptr) {}
        Ref(std::nullptr_t) noexcept : _ptr(nullptr) {}

        Ref(T* ptr) noexcept : _ptr(ptr)
        {
            if (_ptr) _ptr->AddRef();
        }

        Ref(const Ref& other) noexcept : _ptr(other._ptr)
        {
            if (_ptr) _ptr->AddRef();
        }

        template <typename U>
        Ref(const Ref<U>& other) noexcept : _ptr(other.Get())
        {
            if (_ptr) _ptr->AddRef();
        }

        Ref(Ref&& other) noexcept : _ptr(other._ptr)
        {
            other._ptr = nullptr;
        }

        template <typename U>
        Ref(Ref<U>&& other) noexcept : _ptr(other.ReleasePointer())
        {
        }

        ~Ref()
        {
            if (_ptr) _ptr->Release();
        }

        Ref& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        Ref& operator=(T* ptr) noexcept
        {
            if (_ptr != ptr)
            {
                if (ptr) ptr->AddRef();
                if (_ptr) _ptr->Release();
                _ptr = ptr;
            }
            return *this;
        }

        Ref& operator=(const Ref& other) noexcept
        {
            if (this != &other)
            {
                if (other._ptr) other._ptr->AddRef();
                if (_ptr) _ptr->Release();
                _ptr = other._ptr;
            }
            return *this;
        }

        template <typename U>
        Ref& operator=(const Ref<U>& other) noexcept
        {
            if (static_cast<const void*>(this) != static_cast<const void*>(&other))
            {
                if (other.Get()) other.Get()->AddRef();
                if (_ptr) _ptr->Release();
                _ptr = other.Get();
            }
            return *this;
        }

        Ref& operator=(Ref&& other) noexcept
        {
            if (this != &other)
            {
                if (_ptr) _ptr->Release();
                _ptr = other._ptr;
                other._ptr = nullptr;
            }
            return *this;
        }

        template <typename U>
        Ref& operator=(Ref<U>&& other) noexcept
        {
            if (static_cast<const void*>(this) != static_cast<const void*>(&other))
            {
                if (_ptr) _ptr->Release();
                _ptr = other.ReleasePointer();
            }
            return *this;
        }

        void Reset(T* ptr = nullptr) noexcept
        {
            if (ptr != _ptr)
            {
                if (ptr) ptr->AddRef();
                if (_ptr) _ptr->Release();
                _ptr = ptr;
            }
        }

        T* ReleasePointer() noexcept
        {
            T* temp = _ptr;
            _ptr = nullptr;
            return temp;
        }

        void Swap(Ref& other) noexcept { std::swap(_ptr, other._ptr); }

        [[nodiscard]] T* Get() const noexcept { return _ptr; }
        T* operator->() const noexcept { return _ptr; }
        T& operator*() const noexcept { return *_ptr; }

        explicit operator bool() const noexcept { return _ptr != nullptr; }

        bool operator==(const Ref& other) const noexcept { return _ptr == other._ptr; }
        bool operator!=(const Ref& other) const noexcept { return _ptr != other._ptr; }
        bool operator==(T* ptr) const noexcept { return _ptr == ptr; }
        bool operator!=(T* ptr) const noexcept { return _ptr != ptr; }
        bool operator==(std::nullptr_t) const noexcept { return _ptr == nullptr; }
        bool operator!=(std::nullptr_t) const noexcept { return _ptr != nullptr; }

    private:
        T* _ptr;
    };

    template <typename T, typename U>
    inline bool operator==(const Ref<T>& a, const Ref<U>& b) noexcept
    {
        return a.Get() == b.Get();
    }

    template <typename T, typename U>
    inline bool operator!=(const Ref<T>& a, const Ref<U>& b) noexcept
    {
        return a.Get() != b.Get();
    }

    template <typename T, typename... Args>
    Ref<T> MakeRef(Args&&... args)
    {
        return Ref<T>(new T(std::forward<Args>(args)...));
    }

    template <typename T, typename U>
    Ref<T> RefCast(const Ref<U>& other)
    {
        return Ref<T>(dynamic_cast<T*>(other.Get()));
    }

    template <typename T, typename U>
    Ref<T> StaticRefCast(const Ref<U>& other)
    {
        return Ref<T>(static_cast<T*>(other.Get()));
    }
}
