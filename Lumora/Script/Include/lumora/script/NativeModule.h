#pragma once

#include <lumora/Api.h>

#include <jsi/jsi.h>

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace lumora::script
{
    /// Thread-safe. The job runs on the JS thread in the next JsApp::Tick, or is dropped if the runtime it was made
    /// for is gone. Dropped jobs may be destroyed on any thread after that runtime, so they must not hold jsi values.
    using Invoker = std::function<void(std::function<void(facebook::jsi::Runtime&)>)>;

    struct ModuleContext
    {
        facebook::jsi::Runtime& runtime;
        facebook::jsi::Object&  exports; ///< what `requireNativeModule` returns; fill it in
        Invoker                 invoker;
    };

    using InstallFn = void (*)(ModuleContext&);

    /// Native modules by name. JS gets one through `__lumora.requireModule(name)`, installed on first use per runtime.
    class LUMORA_API ModuleRegistry
    {
    public:
        /// A duplicate name logs a warning and keeps the first.
        void Add(std::string name, InstallFn install);
        [[nodiscard]] InstallFn Find(std::string_view name) const;

    private:
        std::map<std::string, InstallFn, std::less<>> m_modules;
    };

    /// Adds every module linked into the executable. Defined exactly once per host executable.
    void RegisterLinkedModules(ModuleRegistry& registry);
}

/// Defines the install function of module `Name`; the body sees `ctx` (a ModuleContext&).
#define LUMORA_NATIVE_MODULE(Name) void LumoraModule_##Name##_Install(::lumora::script::ModuleContext& ctx)
