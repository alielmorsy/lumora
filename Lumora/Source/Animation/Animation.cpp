#include <lumora/Animation/Motion.h>
#include <lumora/Animation/Tween.h>

#include <lumora/log/Log.h>

#include <atomic>

LUMORA_LOG_CATEGORY("Lumora")

namespace lumora
{
    namespace
    {
        std::atomic<float> g_timeScale{1.f};
        std::atomic<bool> g_reducedMotion{false};
    } // namespace

    void SetTimeScale(float scale) noexcept { g_timeScale.store(scale, std::memory_order_relaxed); }
    float TimeScale() noexcept { return g_timeScale.load(std::memory_order_relaxed); }

    void SetReducedMotion(bool enabled) noexcept { g_reducedMotion.store(enabled, std::memory_order_relaxed); }
    bool ReducedMotion() noexcept { return g_reducedMotion.load(std::memory_order_relaxed); }

    namespace detail
    {
        ImperativeRegistry& CurrentImperativeRegistry()
        {
            thread_local ImperativeRegistry registry;
            return registry;
        }

        void WarnAnimationNoOwner(const char* what)
        {
            LOG_WARN("{} was created with no current Owner; it will never advance. Create animations inside "
                     "a Component Render(), OnMount(), or another reactive scope.",
                     what);
        }
    } // namespace detail
} // namespace lumora
