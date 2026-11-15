@echo off
rem Codegen + configure + build EA Sports MMA against its own ReXGlue SDK copy
rem (E:\ClaudeGame\EAMMA-rexglue-sdk, branch eamma = the UFC3 nightly + FNC Reborn's SDK fixes;
rem build it with its build-eamma-sdk.bat).
rem Codegen emits two units: generated\default (default.xex launcher) and
rem generated\mmadllzf_xex (MMADLLzf.xex.dll, the game), built as a host DLL next to eamma.exe.
call "%~dp0env.bat"
cd /d "%~dp0"
set "SDK=E:\ClaudeGame\EAMMA-rexglue-sdk\out\install\win-amd64"
if not exist assets\default.xex (
  echo assets\default.xex missing - assets\ is a junction to E:\ClaudeGame\EAMMA-extract
  exit /b 1
)
"%SDK%\bin\rexglue.exe" codegen eamma_manifest.toml || exit /b 1
cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH="%SDK%" -Drexglue_DIR="%SDK%\lib\cmake\rexglue" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG" || exit /b 1
cmake --build --preset win-amd64-relwithdebinfo || exit /b 1
set "OUT=out\build\win-amd64-relwithdebinfo"
for %%f in (rexruntimerd.dll rexgpu-xenosrd.dll TracyClientrd.dll) do (
  copy /y "%SDK%\bin\%%f" "%OUT%\" >nul || exit /b 1
)
echo.
echo Built: %CD%\%OUT%\eamma.exe
