# Launcher shader cache + Play flow, headless, with fake exes and a fake shader_big.ast. Never starts the real game or
# touches a game folder.
#   launcher\tests\shader-cache-test.ps1 [-Launcher <published "EA MMA Launcher.exe">] [-Work <scratch folder>]
# One fake exe plays three parts by its file name: eamma.exe logs its arguments and exits with fake-game.txt;
# XenosRecomp.exe and eamma_prelink.exe do what fake-tool.txt says ("<XenosRecomp exit> <prelink exit> [nofiles]").
# The launcher's own C# extract runs for real on a one-shader BGFA file made here.
# Native-only: every failure shows the renderer error dialog (EAMMA_TEST_NO_DIALOG=1 here: its text goes to
# launcher-native.log and it counts as Close) and nothing falls back to the emulated renderer.
# Not covered: anything on screen, --setup-iso itself (no ISO here: --shader-cache runs the same step), and the real
# tools (FINDINGS-wp7-wp10.md has the byte-compare against tools/nr_build_shader_cache.py).
param(
  [string]$Launcher,
  [string]$Work = (Join-Path $env:TEMP 'eamma-shader-cache-test')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Launcher) {
  $pub = Join-Path $Work '_publish'
  Write-Host "== dotnet publish"
  dotnet publish "$repo\launcher\EAMMALauncher.csproj" -c Release -o $pub -v q | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "publish failed" }
  $Launcher = Join-Path $pub 'EA MMA Launcher.exe'
}

# Non-ASCII on purpose: the tools get relative arguments, so the folder name must not matter.
$play = Join-Path $Work "play $([char]0xE9)"
if (Test-Path $play) { Remove-Item -Recurse -Force $play }   # plain folder made by this script (no junctions)
$bigs = "$play\assets\data\xenon-gen\bigs"
New-Item -ItemType Directory -Force $play, $bigs | Out-Null
Copy-Item $Launcher $play
$exe = Join-Path $play 'EA MMA Launcher.exe'
Set-Content "$play\assets\default.xex" 'fake'

# A BGFA big with one effect holding one 40-byte shader container (layout: tools/eamma_shader_extract.py).
function Adler32([byte[]]$d) { $a = 1; $b = 0; foreach ($x in $d) { $a = ($a + $x) % 65521; $b = ($b + $a) % 65521 }; [uint32]($b * 65536 + $a) }
$blob = New-Object byte[] 48
$blob[0] = 0xA3; $blob[1] = 0xD7                                   # RNAFX
$blob[8] = 0x10; $blob[9] = 0x2A; $blob[10] = 0x11; $blob[11] = 0  # container at +8: flags, virtual 0x28, physical 0
$blob[15] = 0x28
$ad = Adler32 $blob
# zlib: header, one stored block (final), the data, Adler-32 big-endian
$z = New-Object byte[] (7 + $blob.Length + 4)
$z[0] = 0x78; $z[1] = 0x01; $z[2] = 0x01; $z[3] = $blob.Length; $z[4] = 0; $z[5] = 255 - $blob.Length; $z[6] = 0xFF
$blob.CopyTo($z, 7)
for ($i = 0; $i -lt 4; $i++) { $z[7 + $blob.Length + $i] = [byte](($ad -shr (24 - 8 * $i)) -band 255) }
$big = New-Object byte[] (0x100 + $z.Length)
[Text.Encoding]::ASCII.GetBytes('BGFA').CopyTo($big, 0)
$big[12] = 1                                                        # one entry
$big[0x44 + 9] = 0x20                                               # data offset / 8 = 0x100
[Text.Encoding]::ASCII.GetBytes('fake.fx').CopyTo($big, 0x44 + 17)
$z.CopyTo($big, 0x100)
[IO.File]::WriteAllBytes("$bigs\shader_big.ast", $big)

$src = @'
using System; using System.IO; using System.Linq; using System.Text;
static class Fake {
  static int Main(string[] a) {
    string dir = AppDomain.CurrentDomain.BaseDirectory, me = Path.GetFileNameWithoutExtension(Environment.GetCommandLineArgs()[0]).ToLowerInvariant();
    if (me == "xenosrecomp" || me == "eamma_prelink") {
      File.AppendAllText(Path.Combine(dir, "tool-log.txt"), me + ":" + string.Join("|", a) + ":" + Directory.GetFiles(a[0].EndsWith(".bin") ? "." : a[0]).Length + "\r\n", Encoding.UTF8);
      string[] plan = File.ReadAllText(Path.Combine(dir, "fake-tool.txt")).Trim().Split(' ');
      int code = int.Parse(plan[me == "xenosrecomp" ? 0 : 1]);
      if (plan.Length > 2 && plan[2] == "nofiles") return 0;
      if (code != 0) { Console.Error.WriteLine(me + ": error: fake failure " + code); return code; }
      if (me == "xenosrecomp") {
        // header: magic, version 1, 1 entry, 4 packed bytes, 4 plain bytes; one 24-byte entry; the packed bytes
        var b = new byte[32 + 24 + 4];
        Encoding.ASCII.GetBytes("UFCSHC01").CopyTo(b, 0); b[8] = 1; b[12] = 1; b[16] = 4; b[24] = 4;
        File.WriteAllBytes(a[1], b);
        Console.WriteLine("shader_cache.bin: 1 of 1 shaders");
      } else {
        File.WriteAllText(a[1], "linked");
        Console.WriteLine("prelink: 1 variant(s)");
      }
      return 0;
    }
    File.AppendAllText(Path.Combine(dir, "game-log.txt"), "[" + string.Join(" ", a) + "]\r\n");
    File.WriteAllText(Path.Combine(dir, "game-env.txt"), Environment.GetEnvironmentVariable("EAMMA_LAUNCHER") ?? "");
    int rc = int.Parse(File.ReadAllText(Path.Combine(dir, "fake-game.txt")).Trim());
    if (rc == 3) File.WriteAllText(Path.Combine(dir, "native-error.txt"),
      "code: 3\nwhat: fake device failure\ngpu: Fake GPU (vendor 0x10DE, 8192 MB)\nfeature_level: 11_0\nlog: x.log\n");
    return rc;
  }
}
'@
$fake = Join-Path $play 'eamma.exe'
Add-Type -TypeDefinition $src -OutputAssembly $fake -OutputType ConsoleApplication -ReferencedAssemblies System.Core
Copy-Item $fake "$play\XenosRecomp.exe"
Copy-Item $fake "$play\eamma_prelink.exe"
Set-Content "$play\dxcompiler.dll" 'fake'
Set-Content "$play\dxil.dll" 'fake'
# The launcher's check for native code (NativeRenderer.Marker).
$marker = [Text.Encoding]::ASCII.GetBytes('native: renderer on (experimental')
$f = [IO.File]::Open($fake, 'Append'); $f.Write($marker, 0, $marker.Length); $f.Close()

$toml = "$play\eamma.toml"
$env:EAMMA_TEST_NO_DIALOG = '1'
$fail = 0
$native = '[--eamma_renderer=native]'
function Check($ok, $what) { if ($ok) { Write-Host "  ok   $what" } else { Write-Host "  FAIL $what"; $script:fail++ } }
function Reset($tool, $game = '0') {
  foreach ($n in 'shader_cache.bin', 'shader_cache_linked.bin', 'shader_cache.stamp', 'shader_cache.bin.failures.txt', 'tool-log.txt',
                 'game-log.txt', 'game-env.txt', 'native-error.txt', 'launcher-native.log', 'setup-cli-log.txt') {
    if (Test-Path "$play\$n") { Remove-Item -Force "$play\$n" }
  }
  Set-Content $toml "eamma_native_scale = 2.0"
  Set-Content "$play\fake-tool.txt" $tool
  Set-Content "$play\fake-game.txt" $game
}
function Run([string[]]$a) { (Start-Process $exe -ArgumentList $a -Wait -PassThru).ExitCode }
function GameLog() {
  for ($i = 0; $i -lt 30 -and -not (Test-Path "$play\game-log.txt"); $i++) { Start-Sleep -Milliseconds 100 }
  if (Test-Path "$play\game-log.txt") { (Get-Content "$play\game-log.txt") -join ' ' } else { '' }
}
function Log() { if (Test-Path "$play\launcher-native.log") { Get-Content "$play\launcher-native.log" -Raw -Encoding UTF8 } else { '' } }
function Tools() { if (Test-Path "$play\tool-log.txt") { (Get-Content "$play\tool-log.txt" -Encoding UTF8) -join ' ' } else { '' } }

Write-Host "== Play, no cache, tools work"
Reset '0 0'
[void](Run '--native')
Check ((Test-Path "$play\shader_cache.bin") -and (Test-Path "$play\shader_cache_linked.bin")) "cache built"
Check ((Tools) -eq 'xenosrecomp:in|shader_cache.bin|shader_common.h:2 eamma_prelink:shader_cache.bin|shader_cache_linked.bin:2') "tools got relative arguments; 1 container + _sources.txt extracted: $(Tools)"
Check ((Log) -match '1 effects, 1 containers, 1 unique \(102A1100 x1\)') "extract summary logged"
Check (-not (Test-Path "$play\shader_cache.tmp")) "work folder removed"
Check ((GameLog) -eq $native) "game started with --eamma_renderer=native: $(GameLog)"
Check ((Get-Content "$play\game-env.txt" -Raw) -eq '1') "game told the launcher started it (EAMMA_LAUNCHER=1)"
Check (((Get-Content $toml) -join '') -eq 'eamma_native_scale = 2.0') "eamma.toml untouched"

Write-Host "== Play, cache there: tools not run"
Remove-Item "$play\tool-log.txt", "$play\game-log.txt"
[void](Run '--native')
Check (-not (Test-Path "$play\tool-log.txt")) "tools not run"
Check ((GameLog) -eq $native) "game started"
Check (Test-Path "$play\shader_cache.stamp") "stamp written"

Write-Host "== cache from other tools (update): rebuilt"
Remove-Item "$play\tool-log.txt", "$play\game-log.txt" -ErrorAction SilentlyContinue
$tb = [IO.File]::ReadAllBytes("$play\eamma_prelink.exe")
[IO.File]::WriteAllBytes("$play\eamma_prelink.exe", $tb + [byte[]](0))
[void](Run '--native')
Check (Test-Path "$play\tool-log.txt") "tools run again"
Check ((Log) -match 'rebuilding') "reason logged"
Check ((GameLog) -eq $native) "game started"
Remove-Item "$play\tool-log.txt", "$play\game-log.txt"
[void](Run '--native')
Check (-not (Test-Path "$play\tool-log.txt")) "next run: tools not run"

Write-Host "== cache with no stamp: rebuilt"
Remove-Item "$play\tool-log.txt", "$play\game-log.txt" -ErrorAction SilentlyContinue
Remove-Item "$play\shader_cache.stamp"
[void](Run '--native')
Check (Test-Path "$play\tool-log.txt") "tools run"
Check (Test-Path "$play\shader_cache.stamp") "stamp written"

Write-Host "== a failing step: error dialog, game not started, nothing falls back"
foreach ($t in @('7 0', 4, 'XenosRecomp'), @('0 9', 5, 'eamma_prelink')) {
  Reset $t[0]
  [void](Run '--native')
  Check (-not (Test-Path "$play\game-log.txt")) "$($t[2]) fails: game not started"
  Check (-not (Test-Path "$play\shader_cache.bin")) "$($t[2]) fails: no cache left in place"
  Check ((Log) -match "FAILED" -and (Log) -match "fake failure") "$($t[2]) fails: reason logged"
  Check ((Log) -match "error dialog .*Exit code: $($t[1])" -and (Log) -match 'Log: .*launcher-native\.log') "$($t[2]) fails: dialog names step code $($t[1]) and the log"
}

Write-Host "== a failed rebuild keeps the old cache"
Reset '0 0'
[void](Run '--shader-cache')
$old = (Get-Item "$play\shader_cache.bin").LastWriteTimeUtc
Set-Content "$play\fake-tool.txt" '0 9'
Remove-Item "$play\shader_cache.stamp"
[void](Run '--shader-cache')
Check ((Test-Path "$play\shader_cache.bin") -and (Get-Item "$play\shader_cache.bin").LastWriteTimeUtc -eq $old) "old shader_cache.bin still there"

Write-Host "== tool missing"
Reset '0 0'
Rename-Item "$play\XenosRecomp.exe" 'tool.off'
[void](Run '--native')
Rename-Item "$play\tool.off" 'XenosRecomp.exe'
Check (-not (Test-Path "$play\game-log.txt")) "game not started"
Check ((Log) -match 'error dialog .*is not in the game folder') "dialog says why"

Write-Host "== game files missing"
Reset '0 0'
Rename-Item "$bigs\shader_big.ast" 'big.off'
[void](Run '--native')
Rename-Item "$bigs\big.off" 'shader_big.ast'
Check (-not (Test-Path "$play\game-log.txt")) "game not started"
Check ((Log) -match 'error dialog .*game files .*Exit code: 2') "dialog says the game files are the problem"

Write-Host "== exit 0 but no cache files"
Reset '0 0 nofiles'
[void](Run '--native')
Check (-not (Test-Path "$play\game-log.txt")) "game not started"
Check ((Log) -match 'error dialog') "dialog shown"

Write-Host "== native device failure after a good build: dialog, no emulated restart"
Reset '0 0' '3'
[void](Run '--native')
Start-Sleep -Seconds 1
Check ((GameLog) -eq $native) "ran native once, no second start: $(GameLog)"
Check ((Log) -match 'error dialog .*fake device failure.*Fake GPU.*feature level: 11_0') "dialog: what failed, GPU, feature level"

Write-Host "== after ISO extraction (--shader-cache = the same step)"
Reset '0 0'
Check ((Run '--shader-cache') -eq 0) "exit 0"
Check (Test-Path "$play\shader_cache.bin") "cache built"
Reset '7 0'
Check ((Run '--shader-cache') -eq 0) "failed build: install still exit 0"
Check ((Get-Content "$play\setup-cli-log.txt" -Raw) -match 'Game files installed[\s\S]*could not be built[\s\S]*fake failure 7[\s\S]*step code 4') "failed build: reason + step code in the setup text"
Check (-not (Test-Path "$play\game-log.txt")) "game not started by setup"

Write-Host "== build without native code: setup skips the cache"
$bytes = [IO.File]::ReadAllBytes($fake)
[IO.File]::WriteAllBytes($fake, $bytes[0..($bytes.Length - $marker.Length - 1)])
Reset '7 0'
Check ((Run '--shader-cache') -eq 0) "exit 0"
Check (-not (Test-Path "$play\tool-log.txt")) "tools not run"
Check ((Get-Content "$play\setup-cli-log.txt" -Raw) -notmatch 'could not') "no cache text"

Write-Host ""
if ($fail) { Write-Host "$fail FAILED"; exit 1 } else { Write-Host "all ok"; exit 0 }
