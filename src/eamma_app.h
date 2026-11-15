// eamma - EA Sports MMA, ReXGlue recompiled project
//
// App hooks, bootstrapped from the UFC Undisputed 3 recomp's first setup.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>

REXCVAR_DEFINE_STRING(gpu_backend, "vulkan", "GPU", "Graphics backend: vulkan or d3d12");

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

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // Xenos GPU emulation is a runtime-loaded plugin in v0.10; without it there is no GPU.
    if (!config.graphics) {
      config.graphics = rex::system::LoadGpuPlugin("xenos", REXCVAR_GET(gpu_backend));
      if (!config.graphics) config.gpu_plugin = "xenos";
    }
  }
};
