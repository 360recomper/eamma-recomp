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

# Guest-import stubs the SDK lacks, linked into the game DLL (target from generated/default/dll_targets.cmake).
if(TARGET eamma_MMADLLzf_xex)
    target_sources(eamma_MMADLLzf_xex PRIVATE src/eamma_usbcam.cpp src/eamma_swap_trace.cpp)
endif()
