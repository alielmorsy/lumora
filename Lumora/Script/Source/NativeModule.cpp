#include <lumora/script/NativeModule.h>

#include <lumora/log/Log.h>

LUMORA_LOG_CATEGORY("Script")

namespace lumora::script
{
    void ModuleRegistry::Add(std::string name, InstallFn install)
    {
        if (m_modules.contains(name))
        {
            LOG_WARN("native module '{}' registered twice; keeping the first", name);
            return;
        }
        m_modules.emplace(std::move(name), install);
    }

    InstallFn ModuleRegistry::Find(std::string_view name) const
    {
        const auto it = m_modules.find(name);
        return it == m_modules.end() ? nullptr : it->second;
    }
}
