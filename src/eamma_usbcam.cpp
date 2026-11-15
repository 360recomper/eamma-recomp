// eamma - Xbox Live Vision camera imports (XUsbcam*) for MMADLLzf.xex.dll.
//
// The SDK exports none of them, so the DLL failed to link. EA MMA uses the camera only for the
// optional photo game face; these report "no camera": create fails, state = not connected.
// Compiled into the DLL target (CMakeLists.txt), next to the recompiled code that calls them.

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

namespace {
constexpr uint32_t kErrorDeviceNotConnected = 0x8007048F;  // HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)
}

#define EAMMA_USBCAM_STUB(name, value)                    \
  extern "C" REX_FUNC(__imp__##name) {                    \
    (void)base;                                           \
    static bool logged = false;                           \
    if (!logged) {                                        \
      logged = true;                                      \
      REXLOG_INFO("{}: no camera (stub)", #name);         \
    }                                                     \
    ctx.r3.u64 = (value);                                 \
  }

EAMMA_USBCAM_STUB(XUsbcamCreate, kErrorDeviceNotConnected)
EAMMA_USBCAM_STUB(XUsbcamDestroy, 0)
EAMMA_USBCAM_STUB(XUsbcamGetState, 0)  // state 0: no camera
EAMMA_USBCAM_STUB(XUsbcamReadFrame, kErrorDeviceNotConnected)
EAMMA_USBCAM_STUB(XUsbcamSetCaptureMode, kErrorDeviceNotConnected)
EAMMA_USBCAM_STUB(XUsbcamSetConfig, kErrorDeviceNotConnected)
EAMMA_USBCAM_STUB(XUsbcamSetView, kErrorDeviceNotConnected)
