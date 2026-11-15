// eamma - native renderer startup failures (NATIVE-FINISH-DESIGN.md WP7; from UFC's
// src/native/renderer_fail.h).
//
// When the native renderer can't start, the game says why and exits with a code the launcher reads
// (launcher/NativeRenderer.cs). It does not switch to the emulated renderer on its own. This file is
// part of the exe (EAMMA_SOURCES), not the game DLL: it must work when the DLL or its device fails.
//
// Off switch: --eamma_native_fail_report=false restores the old behaviour (log line + exit code 3,
// no native-error.txt, no message box, no shader cache check).

#pragma once

#include <string>

namespace eamma_renderer_fail {

// Exit codes the launcher reads. The details are in native-error.txt beside the exe.
inline constexpr int kDeviceFailed = 3;
inline constexpr int kCacheMissing = 4;

// Logs the failure, writes native-error.txt beside the exe (what failed, the graphics card and its
// highest Direct3D 12 feature level), shows a message box unless the launcher started the game
// (env EAMMA_LAUNCHER=1; it shows its own dialog from that file) or eamma_native_error_box is
// false (unattended test runs), then exits with `code`.
[[noreturn]] void Fail(int code, const std::string& what);

}  // namespace eamma_renderer_fail
