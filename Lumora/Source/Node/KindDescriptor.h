#pragma once

#include <lumora/Ref.h>

#include "Node/Kind.h"

namespace lumora {

class Widget;

/// Process-wide kind table: one Widget behaviour object per LiveKind; instantiate/measure/paint dispatch through its virtuals (one indirection per kind, never per node).
class KindRegistry {
public:
    /// Binds `kind` to its behaviour object; re-registering replaces the row (one widget instance may serve several kinds).
    static void Register(LiveKind kind, Ref<Widget> widget);

    /// The behaviour object for `kind`, or nullptr for an unregistered (purely structural) kind.
    [[nodiscard]] static Widget* For(LiveKind kind) noexcept;

    [[nodiscard]] static bool Has(LiveKind kind) noexcept;
};

} // namespace lumora
