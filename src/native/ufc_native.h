// Native renderer switch (FINDINGS-native-renderer.md, NR-4 plan > "How the switch works").
//
// eamma_renderer (emulated | native) is read once in Ufc3App::OnPreSetup and latched here. Every
// native hook starts with `if (!g_eamma_native) return;`, so with the default (emulated) the game
// runs exactly as on main. Nothing switches renderers after startup.

#pragma once

inline bool g_eamma_native = false;

// EA MMA (EN-5): registers the game loaded from memory since the last draw go into the device's
// constant block (native/eamma_constant_loads.cpp). Called by every native draw.
void EammaApplyConstantLoads(unsigned int device_guest);
