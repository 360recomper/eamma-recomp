# Short unattended run: start eamma.exe (optionally with an input_script), take a screenshot every
# N seconds into shots\<tag>\, stop after S seconds. Respects the Rumble Roses GPU priority rule.
param([string]$Tag = "run", [string]$Script = "", [int]$Secs = 90, [int]$Every = 10, [string[]]$Extra = @(), [string]$LogLevel = "info")
$p = Split-Path $PSScriptRoot -Parent
if ((Get-Process rumble_roses_xx -ErrorAction SilentlyContinue) -or
    (Test-Path "E:\ClaudeGame\RumbleRosesRecomp\queue-logs\selftest.lock")) { "Rumble Roses holds the GPU; not starting"; exit 2 }
$d = "$p\out\build\win-amd64-relwithdebinfo"
$out = "$p\shots\$Tag"; New-Item -ItemType Directory -Force $out | Out-Null
$log = "$out\run.log"
$argList = @("--game_data_root=`"$($p.Replace('\','/'))/assets`"", "--user_data_root=`"$($p.Replace('\','/'))/userdata`"",
          "--fullscreen=false", "--audio_mute=true", "--window_width=1280", "--window_height=720", "--log_file=`"$($log.Replace('\','/'))`"", "--log_level=$LogLevel") + $Extra
if ($Script) { $argList += "--input_script=`"$($Script.Replace('\','/'))`"" }
$prev = & "$PSScriptRoot\gamectl.ps1" foreground 2>$null
$proc = Start-Process "$d\eamma.exe" -WorkingDirectory $d -PassThru -ArgumentList $argList
# Tuck the window behind the user's windows as soon as it exists (and once more after the first frames).
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 250
  if ((& "$PSScriptRoot\gamectl.ps1" tuck $prev) -eq "tucked") { break }
}
Start-Sleep 2; & "$PSScriptRoot\gamectl.ps1" tuck $prev | Out-Null
for ($t = $Every; $t -le $Secs; $t += $Every) {
  if ($proc.WaitForExit($Every * 1000)) { "exited at ~${t}s code $($proc.ExitCode)"; break }
  & "$PSScriptRoot\gamectl.ps1" shot ("$out\{0:D3}.png" -f $t) -cropW 0 | Out-Null
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; "stopped at ${Secs}s" }
Select-String -Path $log -Pattern "FATAL|unregistered" | Select-Object -Last 2 | % { $_.Line.Substring(0, [Math]::Min(300, $_.Line.Length)) }
