#pragma once

#include <lumora/Host/HostTree.h>

#include <jsi/jsi.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

namespace lumora {
class MountedApp;
}

namespace lumora::script
{
    namespace jsi = facebook::jsi;

    /// A JS function a HostTree handler calls; emptied before its runtime dies, since nodes can outlive it.
    struct JsHandler
    {
        std::optional<jsi::Function> fn;
    };

    /// What `__lumora` and its node refs share with the JsApp that owns the runtime.
    struct BridgeState
    {
        explicit BridgeState(MountedApp& a);

        MountedApp& app;
        HostTree&   host;
        /// Runs a JS call with errors caught and reported; false when it threw or the app has failed.
        std::function<bool(const std::function<void(jsi::Runtime&)>&)> call;

        std::unordered_set<HostHandle>        owned; ///< created by JS and not yet released
        std::vector<std::weak_ptr<JsHandler>> handlers;
        std::size_t                           pruneAt = 64;
        std::atomic<bool>                     tearingDown{false};

        std::mutex              releasedMutex; ///< node ref finalizers may run on a GC thread
        std::vector<HostHandle> released;

        /// Releases the handles JS dropped since the last call. Never from inside a finalizer.
        void DrainReleased();
        /// Releases every handle JS still owns, then clears the root.
        void ReleaseAll();
        /// Empties every JsHandler; call before destroying the runtime.
        void DropHandlers();
    };

    /// Installs the global `__lumora` object.
    void InstallBridge(jsi::Runtime& rt, const std::shared_ptr<BridgeState>& state);
}
