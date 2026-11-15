# Boot-test loop: launch eamma.exe, and while it dies on "Call to invalid or unregistered function",
# seed that address in the right functions file (>= 0x88000000 = MMADLLzf.xex.dll, else default.xex),
# append a NOTES.md row and rebuild. Respects the Rumble Roses GPU priority rule.
param([int]$MaxRounds = 8, [int]$BootSeconds = 60)
$p = Split-Path $PSScriptRoot -Parent
$exeDir = "$p\out\build\win-amd64-relwithdebinfo"
$log = "$p\smoke.log"
for ($i = 1; $i -le $MaxRounds; $i++) {
  if ((Get-Process rumble_roses_xx -ErrorAction SilentlyContinue) -or
      (Test-Path "E:\ClaudeGame\RumbleRosesRecomp\queue-logs\selftest.lock")) {
    "round ${i}: Rumble Roses holds the GPU; stopping"; return
  }
  Remove-Item $log -ErrorAction SilentlyContinue
  $proc = Start-Process -FilePath "$exeDir\eamma.exe" -WorkingDirectory $exeDir -PassThru -ArgumentList `
    "--game_data_root=`"$($p.Replace('\','/'))/assets`"", "--user_data_root=`"$($p.Replace('\','/'))/userdata`"", `
    "--gpu_backend=vulkan", "--fullscreen=false", "--audio_mute=true", "--window_width=1280", "--window_height=720", "--log_file=`"$($log.Replace('\','/'))`"", "--log_level=info"
  $exited = $proc.WaitForExit($BootSeconds * 1000)
  if (-not $exited) {
    Stop-Process -Id $proc.Id -Force
    "round ${i}: still running after ${BootSeconds}s (stopped)"
    return
  }
  $fatal = Select-String -Path $log -Pattern "\[FATAL\].*|unregistered function" | Select-Object -Last 1
  "round ${i}: exit $($proc.ExitCode): $($fatal.Line)"
  if ($fatal.Line -notmatch "unregistered function at guest address (0x[0-9A-Fa-f]+)") { return }
  $addr = "0x" + $Matches[1].Substring(2).ToUpper()
  $isDll = [Convert]::ToUInt32($addr.Substring(2), 16) -ge 0x88000000
  $toml = if ($isDll) { "$p\config\functions_dll.toml" } else { "$p\config\functions_default.toml" }
  if (Select-String -Path $toml -Pattern "^$addr " -Quiet) { "  $addr already seeded; stopping"; return }
  # Already a registered function: the cause is elsewhere (e.g. the DLL never wired), not a missing seed.
  $init = if ($isDll) { "$p\generated\mmadllzf_xex\eamma_init.cpp" } else { "$p\generated\default\eamma_init.cpp" }
  if (Select-String -Path $init -Pattern "\{ $addr," -SimpleMatch:$false -Quiet) { "  $addr is already in the function table; stopping"; return }
  $lr = (Select-String -Path $log -Pattern "lr[ =:]+(0x)?([0-9A-Fa-f]{8})" | Select-Object -Last 1)
  $ctx = if ($lr) { "lr " + $lr.Matches[0].Groups[2].Value } else { "" }
  Add-Content -Encoding utf8 $toml "$addr = { name = `"sub_$($addr.Substring(2))`" }  # runtime: unregistered indirect call $ctx"
  $mod = if ($isDll) { "dll" } else { "default" }
  Add-Content -Encoding utf8 "$p\NOTES.md" "| $mod | $addr | runtime | indirect call target, $ctx (seed_loop round $i) |"
  cmd /c "`"$p\build.bat`" > `"$p\build.log`" 2>&1"
  if ($LASTEXITCODE -ne 0) { "  build failed, see build.log"; return }
  "  seeded $addr and rebuilt"
}
