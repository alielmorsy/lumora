#pragma once

// Export marker for a shared build (LUMORA_SHARED). Empty for the default static build.
#if defined(_WIN32) && defined(LUMORA_SHARED)
    #if defined(LUMORA_BUILDING)
        #define LUMORA_API __declspec(dllexport)
    #else
        #define LUMORA_API __declspec(dllimport)
    #endif
#else
    #define LUMORA_API
#endif

#if defined(_MSC_VER)
    #pragma warning(disable : 4251 4275)
#endif
