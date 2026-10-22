# Masharif: flexbox layout solver used by lumora. Only masharifcore is added: the upstream root also
# builds an executable named "test", which CTest rejects once any dependency enables testing.
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)

lumora_fetch_thirdparty(Masharif
        REPO alielmorsy/Masharif
        TAG v1.1
        CMAKE
        SUBDIR masharifcore)

# masharifcore is SHARED but exports nothing, so MSVC emits no import library. Synthesise the exports;
# IPO must be off or the /GL objects hide the symbols from the export scan.
if (WIN32 AND TARGET masharifcore)
    set_target_properties(masharifcore PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF)
endif ()
