#pragma once

#include <lumora/Paint/DrawTypes.h>

#include <string_view>

namespace lumora {

/// Resolves an image key (file path) to a GPU-resident ImageHandle; decode+upload happen on first Acquire and are cached,
/// refcounted by key. The backend supplies the impl; a headless app has no provider (Image draws nothing).
class ImageProvider {
public:
    virtual ~ImageProvider() = default;

    /// Decode+upload `src` (cached) and bump its refcount; an invalid handle on failure (logged).
    [[nodiscard]] virtual ImageHandle Acquire(std::string_view src) = 0;

    /// Drop one Acquire reference; frees the texture at refcount 0. Unknown key or empty `src` is a no-op.
    virtual void Release(std::string_view src) = 0;
};

}
