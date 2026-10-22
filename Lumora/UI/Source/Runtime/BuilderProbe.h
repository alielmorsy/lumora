#pragma once

#include "Reactive/ReactiveGraph.h"

#include <lumora/Detail/Reactive.h>

#include <lumora/Ref.h>

namespace lumora::detail {

/// Throwaway tracking node made ambient while a build body runs: a bare signal read subscribes it
/// instead of the effect. A build body runs once, so such a read can never patch anything (debug builds warn).
struct BuilderProbe final : ReactiveNode {
    [[nodiscard]] bool TrackedAnything() const noexcept { return HasSources(); }
};

/// RAII: pushes a fresh BuilderProbe as the ambient node for a build body; query TrackedAnything() before scope end.
class BuilderProbeScope {
public:
    explicit BuilderProbeScope(ReactiveGraph* graph) noexcept : m_graph(graph) {
        if (m_graph) {
            m_graph->PushNode(m_probe.Get());
        }
    }
    ~BuilderProbeScope() {
        if (m_graph) {
            m_graph->PopNode();
        }
        m_probe->Dispose();
    }

    BuilderProbeScope(const BuilderProbeScope&)            = delete;
    BuilderProbeScope& operator=(const BuilderProbeScope&) = delete;

    [[nodiscard]] bool TrackedAnything() const noexcept { return m_probe->TrackedAnything(); }

private:
    ReactiveGraph*    m_graph;
    Ref<BuilderProbe> m_probe = MakeRef<BuilderProbe>();
};

} // namespace lumora::detail
