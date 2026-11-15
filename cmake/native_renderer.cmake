# Native renderer (from the SvR 2010 recomp by nefariousjosiah, GPL-3.0, adapted from re:Blue,
# BSD-3-Clause; see CREDITS.md and src/reblue/LICENSE.reblue).
#
# Draws through plume on Direct3D 12 by hooking the game's XDK D3D functions instead of emulating
# the Xenos GPU. Unlike SvR it's a run-time choice (eamma_renderer, src/native/ufc_native.h), so one
# exe carries both renderers and the default stays emulated.
#
# NR-5 (phase 0) builds the pieces that don't depend on UFC addresses: plume, the small third-party
# libraries and the host shaders. The hook sources (src/reblue, src/native/ufc_*.cpp) are in the
# tree but not compiled yet: they need the SDK's null GPU backend (NR-8). EAMMA_NATIVE_HOOKS turns
# them on then.
#
# Game shaders can't ship, so unlike SvR there's no generated shader_cache.cpp: the renderer reads
# shader_cache.bin + shader_cache_linked.bin beside the exe at run time (NR-7b,
# src/reblue/gpu/shaders/shader_cache.h). tools/nr_build_shader_cache.py builds both: XenosRecomp
# writes the first, eamma_prelink (built here) the second. Players have no Python:
# eamma_shader_cache (tools/native/shader_extract, built here) does the same on their machine.
#
# The renderer's sources go straight into the target: its REX_HOOK overrides are strong
# definitions nothing else references, so a static library would drop them at link time.
# EA MMA: the target is the game DLL (eamma_MMADLLzf_xex), not the exe. The D3D library is in
# MMADLLzf.xex.dll, and an override only wins inside the module that holds the weak original
# (NATIVE-PLAN.md point 1, EN-0).

enable_language(C)  # zstd and xxHash are C

set(EAMMA_NATIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(EAMMA_XENOS_DIR "${EAMMA_NATIVE_ROOT}/third_party/XenosRecomp")
set(EAMMA_NATIVE_GEN "${CMAKE_BINARY_DIR}/native_gen")
if(NOT EXISTS "${EAMMA_NATIVE_ROOT}/third_party/plume/CMakeLists.txt" OR
   NOT EXISTS "${EAMMA_XENOS_DIR}/thirdparty/dxc-bin/bin/x64/dxc.exe")
    message(FATAL_ERROR "EAMMA_NATIVE_RENDERER: third_party/plume or XenosRecomp missing; "
                        "run python -I tools/nr_fetch_third_party.py")
endif()

# EA MMA: off until src/native/ufc_d3d_names.h holds EA MMA's addresses (EN-1). A hook compiled with
# UFC's addresses would replace unrelated EA functions (trap 13).
option(EAMMA_NATIVE_HOOKS "Compile the native D3D hooks (off = scaffolding / census build)" OFF)
option(EAMMA_NATIVE_CENSUS "Compile the EN-3 D3D call census (needs the EA MMA map)" OFF)

# D3D12 only: it's our default backend and the laptop tester's only one. Native Vulkan is out
# until the user asks (plume can do it, SvR builds both).
set(PLUME_D3D12_ENABLED ON CACHE BOOL "" FORCE)
add_subdirectory("${EAMMA_NATIVE_ROOT}/third_party/plume" "${CMAKE_BINARY_DIR}/plume" EXCLUDE_FROM_ALL)

# Small third-party pieces the renderer uses.
add_library(eamma_native_thirdparty STATIC
    "${EAMMA_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd/zstddeclib.c"
    "${EAMMA_XENOS_DIR}/thirdparty/xxHash/xxhash.c"
    "${EAMMA_XENOS_DIR}/thirdparty/smol-v/source/smolv.cpp"
    "${EAMMA_NATIVE_ROOT}/third_party/reblue_thirdparty/miniz/miniz.cpp")
target_include_directories(eamma_native_thirdparty PUBLIC
    "${EAMMA_NATIVE_ROOT}/third_party/reblue_thirdparty/zstd"
    "${EAMMA_XENOS_DIR}/thirdparty/xxHash"
    "${EAMMA_XENOS_DIR}/thirdparty/smol-v/source"
    "${EAMMA_NATIVE_ROOT}/third_party/reblue_thirdparty/miniz")

# Host shaders (copy, ImGui overlay, MSAA resolve, gamma) compiled with DXC to DXIL headers.
set(EAMMA_DXC_DIR "${EAMMA_XENOS_DIR}/thirdparty/dxc-bin/bin/x64")
set(EAMMA_DXC "${EAMMA_DXC_DIR}/dxc.exe")
set(EAMMA_HLSL_DIR "${EAMMA_NATIVE_ROOT}/src/reblue/gpu/shaders/hlsl")
set(EAMMA_HOST_SHADER_HEADERS "")
function(eamma_host_shader STEM PROFILE)
    file(GLOB hlsl_includes "${EAMMA_HLSL_DIR}/*.hlsli" "${EAMMA_NATIVE_ROOT}/third_party/fidelityfx/gpu/*.h"
        "${EAMMA_NATIVE_ROOT}/third_party/fidelityfx/gpu/fsr1/*.h")
    set(out "${EAMMA_NATIVE_GEN}/src/gpu/shaders/hlsl/${STEM}.hlsl.dxil.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${EAMMA_DXC}" -T ${PROFILE} -HV 2021 -all-resources-bound -Wno-ignored-attributes
                -I "${EAMMA_NATIVE_ROOT}" -Fh "${out}" "${EAMMA_HLSL_DIR}/${STEM}.hlsl"
                -Vn g_${STEM}_dxil
        DEPENDS "${EAMMA_HLSL_DIR}/${STEM}.hlsl" ${hlsl_includes}
        COMMENT "Compiling ${STEM}.hlsl (${PROFILE}, dxil)"
        VERBATIM)
    set(EAMMA_HOST_SHADER_HEADERS ${EAMMA_HOST_SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()
foreach(shader IN ITEMS copy_vs imgui_vs)
    eamma_host_shader(${shader} vs_6_0)
endforeach()
foreach(shader IN ITEMS copy_color_ps copy_depth_ps gamma_correction_ps fsr_easu_ps fsr_rcas_ps pfx_occlusion_count_ps
        imgui_ps resolve_msaa_color_2x resolve_msaa_color_4x resolve_msaa_color_8x
        resolve_msaa_depth_2x resolve_msaa_depth_4x resolve_msaa_depth_8x)
    eamma_host_shader(${shader} ps_6_0)
endforeach()
# Builds the host shaders with the game even before the hooks include them.
add_custom_target(eamma_host_shaders DEPENDS ${EAMMA_HOST_SHADER_HEADERS})

# DXC's import library for the run-time spec-constant linker (dxc_link.cpp).
add_library(eamma_dxcompiler SHARED IMPORTED)
set_target_properties(eamma_dxcompiler PROPERTIES
    IMPORTED_LOCATION "${EAMMA_DXC_DIR}/dxcompiler.dll"
    IMPORTED_IMPLIB "${EAMMA_XENOS_DIR}/thirdparty/dxc-bin/lib/x64/dxcompiler.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${EAMMA_XENOS_DIR}/thirdparty/dxc-bin/inc")

# D3D12: shaders that read specialization constants are DXIL libraries. eamma_prelink (re:Blue's
# reblue_prelink) DXC-links every variant of a shader_cache.bin into shader_cache_linked.bin, so
# the game never links one while drawing. Built with the game, run by nr_build_shader_cache.py.
add_executable(eamma_prelink
    "${EAMMA_NATIVE_ROOT}/tools/native/prelink_shader_cache.cpp"
    "${EAMMA_NATIVE_ROOT}/src/reblue/gpu/shaders/dxc_link.cpp"
    "${EAMMA_NATIVE_ROOT}/src/reblue/gpu/shaders/shader_cache_file.cpp")
target_include_directories(eamma_prelink PRIVATE "${EAMMA_NATIVE_ROOT}/src/reblue")
target_link_libraries(eamma_prelink PRIVATE eamma_native_thirdparty eamma_dxcompiler)

# UFC's player-PC cache builder (tools/native/shader_extract: xex decrypt + YZLI unpack) reads UFC's
# shader format. EA MMA's source (shader_big.ast or run-time compiled .fx, NATIVE-PLAN.md point 3)
# is decided in EN-4, so it is not built.
include("${CMAKE_CURRENT_LIST_DIR}/xenosrecomp.cmake")

if(EAMMA_NATIVE_HOOKS)
    file(GLOB_RECURSE EAMMA_NATIVE_SOURCES CONFIGURE_DEPENDS
        "${EAMMA_NATIVE_ROOT}/src/reblue/*.cpp" "${EAMMA_NATIVE_ROOT}/src/reblue/*.h"
        "${EAMMA_NATIVE_ROOT}/src/native/*.cpp" "${EAMMA_NATIVE_ROOT}/src/native/*.h")
endif()

function(eamma_add_native_renderer target)
    target_compile_definitions(${target} PRIVATE EAMMA_NATIVE_RENDERER=1)
    target_include_directories(${target} PRIVATE
        "${EAMMA_NATIVE_ROOT}/src/reblue"
        "${EAMMA_NATIVE_ROOT}/src/native"
        "${EAMMA_NATIVE_GEN}"
        "${EAMMA_NATIVE_ROOT}/third_party/plume"
        "${EAMMA_XENOS_DIR}/XenosRecomp")
    target_link_libraries(${target} PRIVATE plume eamma_native_thirdparty)
    add_dependencies(${target} eamma_host_shaders eamma_prelink eamma_xenosrecomp)
    if(NOT EAMMA_NATIVE_HOOKS AND EAMMA_NATIVE_CENSUS)
        # NR-6 D3D call census (--eamma_d3d_census). It overrides the same D3D functions as the
        # native hooks, so it's only built without them; NR-8 moves its counters into the hooks.
        target_sources(${target} PRIVATE "${EAMMA_NATIVE_ROOT}/src/dev/ufc_d3d_census.cpp")
    endif()
    if(EAMMA_NATIVE_HOOKS)
        target_sources(${target} PRIVATE
            ${EAMMA_NATIVE_SOURCES}
            ${EAMMA_HOST_SHADER_HEADERS})
        # SVR_NATIVE_RENDERER selects SvR's observe-the-game's-D3D design in the re:Blue sources.
        target_compile_definitions(${target} PRIVATE REBLUE_D3D12=1 SVR_NATIVE_RENDERER=1 EAMMA_NATIVE_HOOKS=1)
        target_link_libraries(${target} PRIVATE d3d12 dxgi dwmapi eamma_dxcompiler rex::o1heap)
        # UFC's addresses for the D3D functions re:Blue's hooks name (D3DDevice_Clear -> sub_...),
        # then the run-time gate: on the emulated renderer every hook runs only the game's body.
        # eamma_prelink compiles dxc_link.cpp and shader_cache_file.cpp too (no rexglue includes), so
        # they don't get the forced headers; neither names a D3D function.
        set(EAMMA_HOOKED_SOURCES ${EAMMA_NATIVE_SOURCES})
        list(REMOVE_ITEM EAMMA_HOOKED_SOURCES
            "${EAMMA_NATIVE_ROOT}/src/reblue/gpu/shaders/dxc_link.cpp"
            "${EAMMA_NATIVE_ROOT}/src/reblue/gpu/shaders/shader_cache_file.cpp")
        set_source_files_properties(${EAMMA_HOOKED_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-include;${EAMMA_NATIVE_ROOT}/src/native/ufc_d3d_names.h;-include;${EAMMA_NATIVE_ROOT}/src/native/ufc_hook_gate.h")
        # Linker map: names recompiled functions in crash stacks.
        target_link_options(${target} PRIVATE "-Wl,/MAP:${CMAKE_BINARY_DIR}/eamma.map")
    endif()
    # dxcompiler.dll + dxil.dll beside the exe (eamma_prelink, and the runtime DXC linker for any
    # variant the prelink missed).
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${EAMMA_DXC_DIR}/dxcompiler.dll" "${EAMMA_DXC_DIR}/dxil.dll" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    # Pipeline seed (NR-9h, NR-15b): read from beside the shader cache, which defaults to beside the exe.
    # Made by tools/nr_pso_seed.py from test logs; keyed by shader content hashes, no game data in it.
    # EA MMA has none yet (UFC's is keyed by UFC's shader hashes).
    set(seed_bin "${EAMMA_NATIVE_ROOT}/config/native/pipelines_seed.bin")
    if(EXISTS "${seed_bin}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${seed_bin}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()
