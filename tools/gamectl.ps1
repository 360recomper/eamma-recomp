# Drive the eamma window for testing: send keys and grab screenshots.
#   gamectl.ps1 shot <out.png>
#   gamectl.ps1 keys "Return,Down,Down,Space" [holdMs] [gapMs]
param([string]$cmd, [string]$arg, [int]$hold = 80, [int]$gap = 350, [int]$cropW = 1280, [int]$cropH = 720)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  // The game window, not the debug console the process may also own.
  public static IntPtr FindGameWindow(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid) return true;
      var sb = new System.Text.StringBuilder(256); GetWindowText(h, sb, 256);
      if (sb.ToString().StartsWith("eamma [")) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public struct RECT { public int L, T, R, B; }
  public struct POINT { public int X, Y; }
}
"@
[W]::SetProcessDPIAware() | Out-Null

# Needs no game window: run_shots asks for it before the game starts.
if ($cmd -eq 'foreground') { Write-Output ([W]::GetForegroundWindow().ToInt64()); exit 0 }

$p = Get-Process eamma -ErrorAction SilentlyContinue | Select-Object -First 1
$h = if ($p) { [W]::FindGameWindow([uint32]$p.Id) } else { [IntPtr]::Zero }
if ($h -eq [IntPtr]::Zero) { Write-Output "no eamma window"; exit 1 }

# Windows refuses SetForegroundWindow from a background process unless it just
# saw input; a synthetic Alt tap satisfies that check.
function Focus {
  [W]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [W]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 150
}

$vk = @{ Return=0x0D; Space=0x20; Shift=0x10; Esc=0x1B; Escape=0x1B; Tab=0x09; Back=0x08; Backspace=0x08;
         Up=0x26; Down=0x28; Left=0x25; Right=0x27 }

switch ($cmd) {
  'shot' {
    # SW_SHOWNOACTIVATE: un-minimize without taking focus.
    if ([W]::IsIconic($h)) { [W]::ShowWindow($h, 4) | Out-Null; Start-Sleep -Milliseconds 800 }
    $r = New-Object W+RECT; [W]::GetClientRect($h, [ref]$r) | Out-Null
    $pt = New-Object W+POINT; [W]::ClientToScreen($h, [ref]$pt) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    # PW_CLIENTONLY | PW_RENDERFULLCONTENT: works even when the window is covered.
    $hdc = $g.GetHdc(); [W]::PrintWindow($h, $hdc, 3) | Out-Null; $g.ReleaseHdc($hdc)
    if ($cropW -gt 0) { $bmp = $bmp.Clone((New-Object System.Drawing.Rectangle 0, 0, $cropW, $cropH), $bmp.PixelFormat) }
    $bmp.Save($arg, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output "saved $arg ${w}x${hh}"
  }
  'keys' {
    # Posted straight to the window, so it never takes focus (needs mnk_ignore_focus = true).
    # Builds without that cvar need real focus: set EAMMA_FORCE_FOCUS=1.
    if ($env:EAMMA_FORCE_FOCUS -eq '1') { Focus }
    foreach ($k in $arg.Split(',')) {
      $k = $k.Trim()
      if ($k -match '^wait(\d+)$') { Start-Sleep -Milliseconds ([int]$matches[1]); continue }
      $code = if ($vk.ContainsKey($k)) { $vk[$k] } else { [int][char]$k.ToUpper() }
      $sc = [W]::MapVirtualKey($code, 0)
      $ext = if ($code -in 0x25..0x28) { 1 } else { 0 }
      $down = 1 -bor ($sc -shl 16) -bor ($ext -shl 24)
      $up = $down -bor (1 -shl 30) -bor (1 -shl 31)
      [W]::PostMessage($h, 0x100, [IntPtr]$code, [IntPtr]$down) | Out-Null
      Start-Sleep -Milliseconds $hold
      [W]::PostMessage($h, 0x101, [IntPtr]$code, [IntPtr][long]$up) | Out-Null
      Start-Sleep -Milliseconds $gap
    }
    Write-Output "sent $arg"
  }
  'tuck' {
    # Push the game window behind everything without activating it, then give focus back to the
    # window that had it (arg = that window's handle). Keeps test runs off the user's screen.
    $HWND_BOTTOM = [IntPtr]1; $SWP_NOSIZE = 0x1; $SWP_NOACTIVATE = 0x10
    [W]::SetWindowPos($h, $HWND_BOTTOM, 40, 40, 0, 0, $SWP_NOSIZE -bor $SWP_NOACTIVATE) | Out-Null
    if ($arg) { [W]::SetForegroundWindow([IntPtr][long]$arg) | Out-Null }
    Write-Output "tucked"
  }
  'focus' { Focus; Write-Output "focused" }
  default { Write-Output "usage: shot <png> | keys <k1,k2,...>" }
}
