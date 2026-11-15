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

#include <windows.h>

#include <string>

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
};
