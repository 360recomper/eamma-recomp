// EN-5: the app (eamma.exe) <-> native renderer (game DLL) bridge.
//
// The renderer is compiled into eamma_MMADLLzf_xex.dll, where the game's D3D library lives
// (NATIVE-PLAN.md EN-0); the window, the GPU plugin choice and the run-time latch belong to the app
// in the exe (src/eamma_app.h). The exe loads this DLL early (LoadLibrary, same module the SDK loads
// for the game later) and calls these C exports. g_eamma_native lives here, in the DLL, where every
// hook gate reads it.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window.h>

#include "gpu/device.h"
#include "ufc_native.h"

void SvrInstallCrashTrace(const char* trace_dir);  // crash_trace.cpp

extern "C" {

__declspec(dllexport) void EammaNativeEnable(bool on) {
  g_eamma_native = on;
  REXLOG_INFO("native: hooks {} (game DLL)", on ? "on" : "off");
}

__declspec(dllexport) void EammaNativeInstallCrashTrace(const char* dir) { SvrInstallCrashTrace(dir); }

__declspec(dllexport) bool EammaNativeCreateDevice(rex::ui::Window* window) {
  return bd::gpu::Video::CreateHostDevice(window);
}

__declspec(dllexport) void EammaNativeRequestResize() { bd::gpu::Video::RequestResize(); }

}  // extern "C"
