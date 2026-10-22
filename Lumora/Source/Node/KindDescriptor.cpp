#include "Node/KindDescriptor.h"

#include "Widgets/Widget.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace lumora {

namespace {

std::vector<Ref<Widget>>& Table() {
    static std::vector<Ref<Widget>> table;
    return table;
}

} // namespace

void KindRegistry::Register(LiveKind kind, Ref<Widget> widget) {
    auto& table = Table();
    const auto index = static_cast<std::size_t>(kind);
    if (index >= table.size()) {
        table.resize(index + 1);
    }
    table[index] = std::move(widget);
}

Widget* KindRegistry::For(LiveKind kind) noexcept {
    auto& table = Table();
    const auto index = static_cast<std::size_t>(kind);
    return index < table.size() ? table[index].Get() : nullptr;
}

bool KindRegistry::Has(LiveKind kind) noexcept {
    return For(kind) != nullptr;
}

} // namespace lumora
