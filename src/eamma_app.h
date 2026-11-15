// eamma - EA Sports MMA, ReXGlue recompiled project
//
// App hooks, bootstrapped from the UFC Undisputed 3 recomp's first setup.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>

#include <windows.h>

#include <filesystem>
#include <string>

#include "eamma_renderer_fail.h"

// Defaults Fight Night Champion (same EA engine family) runs with, from FNC Reborn's FNCReborn.toml
// (whompay/FNCReborn). Applied unless the same cvar is on the command line.
namespace eamma_defaults {
inline bool OnCommandLine(const char* name) {
  std::wstring cmd = GetCommandLineW();
  std::wstring key = L"--";
  for (const char* c = name; *c; ++c) key += static_cast<wchar_t>(*c);
  return cmd.find(key + L"=") != std::wstring::npos || cmd.find(key + L" ") != std::wstring::npos;
}
inline void Apply() {
  static const std::pair<const char*, const char*> kDefaults[] = {
      {"gpu_backend", "d3d12"},
      {"render_target_path_d3d12", "rtv"},
      {"use_fuzzy_alpha_epsilon", "true"},
      {"gamma_render_target_as_unorm16", "true"},
      {"d3d12_readback_resolve", "false"},
      {"readback_resolve", "smart"},
      {"readback_resolve_smart_skip_steady", "true"},
      {"primitive_processor_cache_min_indices", "1000000"},
  };
  for (const auto& [name, value] : kDefaults) {
    if (OnCommandLine(name)) continue;
    if (!rex::cvar::SetFlagByName(name, value)) {
      REXLOG_DEBUG("eamma: {} not registered yet", name);
    } else {
      REXLOG_DEBUG("eamma: {} = {}", name, value);
    }
  }
}
}  // namespace eamma_defaults

// gpu_backend is defined by the EA MMA SDK copy (FNC Reborn's rex_app.cpp); eamma_defaults sets
// d3d12: FNC's "smart" resolve readback is D3D12 only.
REXCVAR_DECLARE(std::string, gpu_backend);
REXCVAR_DEFINE_BOOL(eamma_invalid_fetch, true, "eamma",
                    "Set gpu_allow_invalid_fetch_constants (sample textures whose fetch constant is typed invalid)");

// Native renderer switch (NATIVE-PLAN.md). Read once in LatchRenderer; nothing switches later.
// The renderer itself is compiled into the game DLL (cmake/native_renderer.cmake); EN-5 adds the
// bridge that hands the latch, the window and the null guest GPU over to it.
REXCVAR_DEFINE_STRING(eamma_renderer, "emulated", "eamma",
                      "Renderer: emulated (Xenos GPU emulation, default) or native (plume D3D12, not "
                      "implemented yet)");
// WP7 release set (src/eamma_renderer_fail.h). The launcher reads the exit code and native-error.txt.
REXCVAR_DEFINE_BOOL(eamma_native_fail_report, true, "eamma",
                    "Native renderer: when it can't start (no shader cache, no Direct3D 12 device), write "
                    "native-error.txt beside the exe, tell the player and exit with code 3 (device) or 4 "
                    "(shader cache). false = the old behaviour: a log line and exit code 3, no cache check");
REXCVAR_DEFINE_BOOL(eamma_native_test_device_fail, false, "eamma",
                    "Test only: native acts as if the host device failed (exit code 3)");

class EammaApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<EammaApp>(new EammaApp(ctx, "eamma", PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    // SDK v0.10 doesn't default the game mount: use assets\ next to the exe
    // unless --game_data_root was given.
    if (paths.game_data_root.empty()) {
      auto assets_dir = rex::filesystem::GetExecutableFolder() / "assets";
      if (std::filesystem::is_directory(assets_dir)) paths.game_data_root = assets_dir;
    }
  }

  // The launcher's LoadLibrary("MMADLLzf.xex.dll") is joined onto the exe's path, so the kernel
  // loads "\Device\Harddisk0\Partition1\MMADLLzf.xex.dll". The generated registry keys the host
  // DLL by "mmadllzf.xex.dll" only, the lookup misses, and DllMain then dies "function not in
  // function table". Register the device path too (the SDK normalizes and lowercases both).
  void OnPostLoadXexImage() override {
    auto* ks = REX_KERNEL_STATE();
    auto* exe = ks->GetExecutableModule().get();
    REXLOG_INFO("eamma: executable module path '{}'", exe ? exe->path() : std::string("?"));
    for (const char* alias : {"\\Device\\Harddisk0\\Partition1\\MMADLLzf.xex.dll",
                              "\\Device\\Cdrom0\\MMADLLzf.xex.dll"}) {
      ks->RegisterRecompiledModule("mmadllzf.xex.dll", alias, "eamma_MMADLLzf_xex");
    }
  }

  // Native when asked for and compiled in; every other case runs the emulated renderer.
  static bool OnCommandLine(const char* name) { return eamma_defaults::OnCommandLine(name); }

  static bool LatchRenderer() {
    const std::string& renderer = REXCVAR_GET(eamma_renderer);
    if (renderer == "native") {
#if defined(EAMMA_NATIVE_HOOKS)
      REXLOG_INFO("native: renderer on (experimental, plume D3D12)");
      return true;
#elif defined(EAMMA_NATIVE_RENDERER)
      // Scaffolding / census build (EAMMA_NATIVE_HOOKS off): no native hooks compiled.
      REXLOG_WARN("native: not implemented, using the emulated renderer");
#else
      REXLOG_WARN("native: this build has no native renderer, using the emulated renderer");
#endif
    } else if (renderer != "emulated") {
      REXLOG_WARN("eamma_renderer '{}' unknown, using the emulated renderer", renderer);
    }
    return false;
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    native_ = LatchRenderer();
#if defined(EAMMA_NATIVE_HOOKS)
    if (native_ && !native_bridge_.Load()) {
      if (REXCVAR_GET(eamma_native_fail_report)) {
        eamma_renderer_fail::Fail(eamma_renderer_fail::kDeviceFailed,
                                  "the native renderer could not be loaded from eamma_MMADLLzf_xex.dll");
      }
      REXLOG_WARN("native: using the emulated renderer (eamma_native_fail_report is off)");
      native_ = false;
    }
    if (native_ && REXCVAR_GET(eamma_native_fail_report)) {
      // No shader cache (eamma_shader_cache, relative = beside the exe; the cvar is in the game DLL,
      // loaded just above): native would draw nothing. The launcher builds it before every Play.
      std::filesystem::path cache = rex::to_path(rex::cvar::GetFlagByName("eamma_shader_cache"));
      if (cache.empty()) cache = "shader_cache.bin";
      if (cache.is_relative()) cache = rex::filesystem::GetExecutableFolder() / cache;
      std::error_code ec;
      if (!std::filesystem::is_regular_file(cache, ec)) {
        eamma_renderer_fail::Fail(eamma_renderer_fail::kCacheMissing,
                                  "the shader cache is missing (" + rex::path_to_utf8(cache) + ")");
      }
    }
    if (native_) {
      native_bridge_.enable(true);
      // EN-3: the game's command-buffer objects are drawn directly (its own fallback path), so the
      // renderer sees every draw through the hooked draw calls.
      if (!OnCommandLine("eamma_command_buffers")) rex::cvar::SetFlagByName("eamma_command_buffers", "false");
      // The Xenos plugin's null backend keeps the game's D3D command stream moving (fences,
      // interrupts, vblank) without drawing; the hooks draw through plume.
      if (!config.graphics) {
        config.graphics = rex::system::LoadGpuPlugin("xenos", "null");
        config.gpu_plugin = "xenos";
        REXLOG_INFO("native: guest GPU {}", config.graphics ? "null (sync only)" : "MISSING");
      }
      return;
    }
#endif
    eamma_defaults::Apply();
    // Xenos GPU emulation is a runtime-loaded plugin in v0.10; without it there is no GPU.
    if (!config.graphics) {
      config.graphics = rex::system::LoadGpuPlugin("xenos", REXCVAR_GET(gpu_backend));
      if (!config.graphics) config.gpu_plugin = "xenos";
    }
    eamma_defaults::Apply();  // again: the GPU settings exist only once the plugin is loaded
    // Like UFC3: the game samples textures through fetch constants typed "invalid" (first boot:
    // 467k warnings in 100 s for one real texture, 84024800 04AB3054 ...). The 360 samples them
    // anyway; the emulator default treats them as unbound, and the warning spam alone slowed the
    // boot. The flag lives in the GPU plugin, so set it after the plugin is loaded.
    if (REXCVAR_GET(eamma_invalid_fetch) &&
        !rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true")) {
      REXLOG_WARN("could not enable gpu_allow_invalid_fetch_constants");
    }
  }
#if defined(EAMMA_NATIVE_HOOKS)
  // Exit code when the native device can't be created; the launcher shows why from
  // native-error.txt (src/eamma_renderer_fail.h). Never a switch to the emulated renderer.
  static constexpr int kNativeDeviceFailedExitCode = eamma_renderer_fail::kDeviceFailed;

  void OnPreLaunchModule() override {
    if (!native_) return;
    std::filesystem::path dir = rex::filesystem::GetExecutableFolder();
    const std::string log_file = rex::cvar::GetFlagByName("log_file");
    if (!log_file.empty() && !std::filesystem::path(log_file).parent_path().empty())
      dir = std::filesystem::path(log_file).parent_path();
    native_bridge_.crash_trace(dir.string().c_str());
    REXLOG_INFO("native: crash and hang traces go to {}", dir.string());
    const bool report = REXCVAR_GET(eamma_native_fail_report);
    if (REXCVAR_GET(eamma_native_test_device_fail)) {
      if (report) {
        eamma_renderer_fail::Fail(eamma_renderer_fail::kDeviceFailed,
                                  "test: eamma_native_test_device_fail is set");
      }
      REXLOG_ERROR("native: test: eamma_native_test_device_fail is set");
      rex::FlushLogging();
      std::_Exit(kNativeDeviceFailedExitCode);
    }
    if (!native_bridge_.create_device(window())) {
      if (report) {
        eamma_renderer_fail::Fail(eamma_renderer_fail::kDeviceFailed,
                                  "Direct3D 12 device creation failed");
      }
      REXLOG_ERROR("native: host device creation failed");
      rex::FlushLogging();
      std::_Exit(kNativeDeviceFailedExitCode);
    }
  }

  void OnWindowPixelSizeChanged(uint32_t w, uint32_t h) override {
    if (!native_) {
      rex::ReXApp::OnWindowPixelSizeChanged(w, h);
      return;
    }
    native_bridge_.request_resize();
  }
#endif

 private:
  bool native_ = false;
#if defined(EAMMA_NATIVE_HOOKS)
  // The renderer is in the game DLL (src/native/eamma_bridge.cpp). Loading it here gives the same
  // module the SDK loads for the game later (same path, LoadLibrary reference count).
  struct NativeBridge {
    void (*enable)(bool) = nullptr;
    void (*crash_trace)(const char*) = nullptr;
    bool (*create_device)(rex::ui::Window*) = nullptr;
    void (*request_resize)() = nullptr;
    bool Load() {
      const auto path = rex::filesystem::GetExecutableFolder() / "eamma_MMADLLzf_xex.dll";
      HMODULE m = LoadLibraryW(path.wstring().c_str());
      if (m) {
        enable = reinterpret_cast<void (*)(bool)>(GetProcAddress(m, "EammaNativeEnable"));
        crash_trace = reinterpret_cast<void (*)(const char*)>(GetProcAddress(m, "EammaNativeInstallCrashTrace"));
        create_device = reinterpret_cast<bool (*)(rex::ui::Window*)>(GetProcAddress(m, "EammaNativeCreateDevice"));
        request_resize = reinterpret_cast<void (*)()>(GetProcAddress(m, "EammaNativeRequestResize"));
      }
      const bool ok = enable && crash_trace && create_device && request_resize;
      if (!ok) REXLOG_ERROR("native: renderer exports missing in {}", path.string());
      return ok;
    }
  };
  NativeBridge native_bridge_;
#endif
};
