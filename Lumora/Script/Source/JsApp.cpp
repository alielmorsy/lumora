#include <lumora/script/JsApp.h>

#include "Bridge.h"

#include <lumora/MountedApp.h>
#include <lumora/log/Log.h>

#include <hermes/hermes.h>
#include <jsi/instrumentation.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

LUMORA_LOG_CATEGORY("Script")

namespace lumora::script
{
    namespace
    {
        using Job = std::function<void(jsi::Runtime&)>;

        /// Shared with every Invoker, which may outlive the JsApp. Jobs from an older generation are dropped.
        struct JobQueue
        {
            std::mutex       mutex;
            std::uint64_t    generation = 0;
            std::vector<Job> jobs;
        };
    }

    struct JsApp::Impl
    {
        MountedApp&                                   app;
        const ModuleRegistry*                         modules;
        std::shared_ptr<JobQueue>                     queue = std::make_shared<JobQueue>();
        std::unique_ptr<facebook::hermes::HermesRuntime> runtime;
        std::shared_ptr<BridgeState>                  state;
        std::string                                   lastError;
        bool                                          failed = false;
        std::optional<double>                         baseMs; ///< nowMs of the first Tick after Load

        Impl(MountedApp& a, const ModuleRegistry* mods) : app(a), modules(mods) {}

        /// Runs `fn` then the microtasks; any error stops the app and shows the overlay. Never throws.
        bool Call(const std::function<void(jsi::Runtime&)>& fn)
        {
            if (!runtime || failed) return false;
            try
            {
                fn(*runtime);
                runtime->drainMicrotasks();
                state->DrainReleased();
                return true;
            }
            catch (const jsi::JSError& e)
            {
                Fail(e.getMessage(), e.getMessage() + "\n" + e.getStack());
            }
            catch (const std::exception& e) // also jsi::JSIException: syntax errors arrive as "Compiling JS failed: ..."
            {
                Fail(e.what(), e.what());
            }
            return false;
        }

        /// Call from outside a dispatch.
        bool Enter(const std::function<void(jsi::Runtime&)>& fn)
        {
            bool ok = false;
            app.RunScoped([&] { ok = Call(fn); });
            return ok;
        }

        void Fail(const std::string& message, std::string full)
        {
            failed = true;
            lastError = std::move(full);
            LOG_ERROR("{}", lastError);

            state->ReleaseAll();
            HostTree& host = state->host;
            const HostHandle t = host.Create(HostTag::Text);
            host.SetProp(t, "class", std::string("lumora-error"));
            host.SetProp(t, "style", std::string("color: #ff5555; padding: 12px"));
            host.SetProp(t, "text", message);
            host.Insert(host.Root(), t);
        }
    };

    JsApp::JsApp(MountedApp& app, const ModuleRegistry* modules) : m_impl(std::make_unique<Impl>(app, modules)) {}

    JsApp::~JsApp() { Unload(); }

    bool JsApp::Load(std::string_view source, std::string_view sourceUrl)
    {
        Unload();
        Impl& m = *m_impl;
        m.lastError.clear();
        m.failed = false;
        m.baseMs.reset();
        m.runtime = facebook::hermes::makeHermesRuntime(::hermes::vm::RuntimeConfig::Builder().withMicrotaskQueue(true).build());
        m.state = std::make_shared<BridgeState>(m.app);
        m.state->call = [&m](const std::function<void(jsi::Runtime&)>& fn) { return m.Call(fn); };
        m.state->modules = m.modules;
        m.state->invoker = [q = m.queue, gen = m.queue->generation](Job job)
        {
            std::lock_guard lock(q->mutex);
            if (q->generation == gen) q->jobs.push_back(std::move(job));
        };
        InstallBridge(*m.runtime, m.state);

        auto buffer = std::make_shared<jsi::StringBuffer>(std::string(source));
        return m.Enter([&](jsi::Runtime& rt) { rt.evaluateJavaScript(buffer, std::string(sourceUrl)); });
    }

    void JsApp::Tick(double nowMs)
    {
        Impl& m = *m_impl;
        if (!m.runtime || m.failed) return;
        // Timers count from the load, so a reload never fires the old app's due timers on its first frame.
        if (!m.baseMs) m.baseMs = nowMs;
        const double t = nowMs - *m.baseMs;
        std::vector<Job> jobs;
        {
            std::lock_guard lock(m.queue->mutex);
            jobs.swap(m.queue->jobs);
        }
        m.Enter([&jobs, t](jsi::Runtime& rt)
        {
            for (const Job& job : jobs) job(rt);
            const jsi::Value tick = rt.global().getProperty(rt, "__lumoraTick");
            if (tick.isObject() && tick.getObject(rt).isFunction(rt)) tick.getObject(rt).getFunction(rt).call(rt, t);
        });
    }

    void JsApp::Unload()
    {
        Impl& m = *m_impl;
        if (!m.runtime) return;
        std::vector<Job> stale; // destroyed outside the lock
        {
            std::lock_guard lock(m.queue->mutex);
            ++m.queue->generation;
            stale.swap(m.queue->jobs);
        }
        // Handlers hold jsi::Functions and may sit on nodes that outlive the runtime; node ref finalizers stay quiet.
        m.state->tearingDown = true;
        m.state->DropHandlers();
        m.runtime.reset();
        m.state->ReleaseAll();
        m.state.reset();
    }

    bool JsApp::Loaded() const noexcept { return m_impl->runtime && !m_impl->failed; }

    const std::string& JsApp::LastError() const noexcept { return m_impl->lastError; }

    void JsApp::CollectGarbageForTesting()
    {
        Impl& m = *m_impl;
        if (!m.runtime) return;
        m.runtime->instrumentation().collectGarbage("test");
        m.state->DrainReleased();
    }
}
