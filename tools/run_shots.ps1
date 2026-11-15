# Short unattended run: start eamma.exe (optionally with an input_script), take a screenshot every
# N seconds into shots\<tag>\, stop after S seconds. Respects the Rumble Roses GPU priority rule.
param([string]$Tag = "run", [string]$Script = "", [int]$Secs = 90, [int]$Every = 10, [string[]]$Extra = @())
$p = Split-Path $PSScriptRoot -Parent
if ((Get-Process rumble_roses_xx -ErrorAction SilentlyContinue) -or
    (Test-Path "E:\ClaudeGame\RumbleRosesRecomp\queue-logs\selftest.lock")) { "Rumble Roses holds the GPU; not starting"; exit 2 }
$d = "$p\out\build\win-amd64-relwithdebinfo"
$out = "$p\shots\$Tag"; New-Item -ItemType Directory -Force $out | Out-Null
$log = "$out\run.log"
$argList = @("--game_data_root=`"$($p.Replace('\','/'))/assets`"", "--user_data_root=`"$($p.Replace('\','/'))/userdata`"",
          "--gpu_backend=vulkan", "--log_file=`"$($log.Replace('\','/'))`"", "--log_level=info") + $Extra
if ($Script) { $argList += "--input_script=`"$($Script.Replace('\','/'))`"" }
$proc = Start-Process "$d\eamma.exe" -WorkingDirectory $d -PassThru -ArgumentList $argList
for ($t = $Every; $t -le $Secs; $t += $Every) {
  if ($proc.WaitForExit($Every * 1000)) { "exited at ~${t}s code $($proc.ExitCode)"; break }
  & "$PSScriptRoot\gamectl.ps1" shot ("$out\{0:D3}.png" -f $t) -cropW 0 | Out-Null
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; "stopped at ${Secs}s" }
Select-String -Path $log -Pattern "FATAL|unregistered" | Select-Object -Last 2 | % { $_.Line.Substring(0, [Math]::Min(300, $_.Line.Length)) }
