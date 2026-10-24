#pragma once

#include <lumora/Api.h>

#include <memory>
#include <string>
#include <string_view>

namespace lumora {
class MountedApp;
}

namespace lumora::script
{
    /// One JS app on Hermes, driving `app.Host()` through the global `__lumora`. Destroy it before the MountedApp.
    /// A JS error (load, tick, handler or microtask) shows a red overlay and stops the app until the next Load.
    class LUMORA_API JsApp
    {
    public:
        explicit JsApp(MountedApp& app);
        ~JsApp();

        JsApp(const JsApp&)            = delete;
        JsApp& operator=(const JsApp&) = delete;

        /// Unloads, then evaluates `source` in a fresh runtime with `__lumora` installed; false on error.
        bool Load(std::string_view source, std::string_view sourceUrl);
        /// Calls the global `__lumoraTick(ms since the first Tick after Load)` if defined, then drains microtasks.
        void Tick(double nowMs);
        /// Destroys the runtime, then frees every node the script built.
        void Unload();
        /// A script is loaded and has not failed.
        [[nodiscard]] bool Loaded() const noexcept;
        /// Message and JS stack of the last failure; empty if none since Load.
        [[nodiscard]] const std::string& LastError() const noexcept;

        void CollectGarbageForTesting();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
