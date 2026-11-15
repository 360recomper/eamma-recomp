# eamma - EA Sports MMA, ReXGlue recompiled project
#
# SDK boilerplate (and the generated DLL target for MMADLLzf.xex.dll) lives in
# generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(eamma LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

set(EAMMA_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(eamma WIN32 ${EAMMA_SOURCES})
else()
    add_executable(eamma ${EAMMA_SOURCES})
endif()

rexglue_setup_target(eamma GPU_PLUGINS xenos)

# Diagnostics linked into the game DLL (camera imports now come from the EA MMA SDK copy) (target from generated/default/dll_targets.cmake).
if(TARGET eamma_MMADLLzf_xex)
    target_sources(eamma_MMADLLzf_xex PRIVATE src/eamma_swap_trace.cpp)
    # EN-0 hook proof on D3DDevice_Swap; the native hooks own Swap when they are compiled in.
    if(NOT EAMMA_NATIVE_HOOKS)
        target_sources(eamma_MMADLLzf_xex PRIVATE src/dev/eamma_hook_proof.cpp)
    endif()
endif()

# Native renderer (NATIVE-PLAN.md, cmake/native_renderer.cmake): compiled into the game DLL, where
# the D3D library lives, and picked at run time with eamma_renderer=native; the default stays
# emulated. build.bat native / nonative sets it.
option(EAMMA_NATIVE_RENDERER "Compile in the native (plume D3D12) renderer" OFF)
if(EAMMA_NATIVE_RENDERER AND TARGET eamma_MMADLLzf_xex)
    include(cmake/native_renderer.cmake)
    eamma_add_native_renderer(eamma_MMADLLzf_xex)
    # The app (src/eamma_app.h, in the exe) only needs to know what was compiled in.
    target_compile_definitions(eamma PRIVATE EAMMA_NATIVE_RENDERER=1)
    if(EAMMA_NATIVE_HOOKS)
        target_compile_definitions(eamma PRIVATE EAMMA_NATIVE_HOOKS=1)
    endif()
endif()
