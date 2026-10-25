# Template for a game's top-level CMakeLists.txt. Placeholders:
#   PolicenautsRecomp  CMake project name, e.g. Tomba2Recomp
#   Policenauts    window title, e.g. Tomba! 2
#   SLPS_002.15      boot EXE file name from game.toml [game] exe, e.g. SCUS_944.54
cmake_minimum_required(VERSION 3.20)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR
        "PSXRecomp runtime is missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake\n")
endif()

project(PolicenautsRecomp LANGUAGES C CXX)

if(ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Keep the debug APK debuggable while optimizing the CPU-bound PS1 runtime.
    # The Android debug variant otherwise compiles the emulator at -O0, which
    # causes severe slowdown on a phone. ThinLTO then lets Clang optimize hot
    # call paths across the generated game code and runtime without changing
    # emulation semantics.
    add_compile_options(-O3 -flto=thin)
    add_link_options(-flto=thin)
endif()
if(ANDROID)
    # libmain.so is the whole game; nothing interposes its symbols. Bind its
    # internal references directly: calls skip the PLT hop (profiled at ~4% on
    # the dispatch-heavy hot path). Hidden visibility lets the compiler address
    # runtime globals PC-relative instead of loading each address from the GOT
    # first; the per-instruction cycle helpers touch several globals per guest
    # instruction, so that dependent load is real cost (lld does not relax the
    # interleaved adrp/ldr GOT pairs Clang emits). Only JNIEXPORT functions and
    # SDL_main (SDL_DECLSPEC) need to stay visible; overlay shards import
    # nothing from libmain (they go through the overlay_api callback table).
    add_compile_options(-fno-semantic-interposition -fvisibility=hidden
                        $<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>)
    add_link_options(-Wl,-Bsymbolic)
endif()

set(PSX_REWIND OFF CACHE BOOL "Disable Rewind" FORCE)
set(PSX_RECOMP_UI OFF CACHE BOOL "Disable UI" FORCE)
if(ANDROID)
    set(PSX_ENABLE_VULKAN OFF CACHE BOOL "Disable Vulkan on Android" FORCE)
endif()
if(PROSPERO)
    # PS5: the title's data folder (game.toml, disc/, saves/, psx_ps5.log).
    set(PSX_PS5_DATA_DIR "/data/policenauts")
endif()
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

set(PSX_PRELOADED_MODS "NONE")

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR "${PSX_PRELOADED_MODS}"
    WINDOW_TITLE "Policenauts"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 1
    GEN_MARKER "generated/SLPS_002.15_dispatch.c"
    # Pre-compiled overlays (build-ps5-overlays/, compile_overlays.py --static).
    # Must precede GEN_FULL_GLOB: that list keyword swallows what follows it.
    GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c"
    GEN_FULL_GLOB "generated/SLPS_002.15_full_*.c"
)

if(PROSPERO)
    # Default game.toml / .cue files and boot disc for the PS5 data folder.
    target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ps5/ps5_game.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/ps5/ps5_bios_blob.c")
endif()

# Android's SDLActivity loads the game entry point from libmain.so. The runtime
# helper keeps the normal desktop executable target on PC and switches only the
# Android build to SDL's shared-library contract.
if(ANDROID)
    set_target_properties(psx-runtime PROPERTIES OUTPUT_NAME main)
endif()

foreach(_PSX_RUNTIME_CONFIG game.toml game_options.toml)
    # Android takes its config from the APK's assets/game.toml.in instead.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}" AND NOT ANDROID)
        add_custom_command(TARGET psx-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()
