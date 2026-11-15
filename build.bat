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
rem build.bat native / census / nonative (kept in the CMake cache):
rem   native   = the native renderer in the game DLL (run with --eamma_renderer=native; the default
rem              stays emulated). Needs the shader cache: python -I tools\nr_build_shader_cache.py
rem   census   = the EN-3 D3D call census instead of the renderer (--eamma_d3d_census=true)
rem   nonative = neither
set "NATIVE_ARG="
if /i "%~1"=="native" set "NATIVE_ARG=-DEAMMA_NATIVE_RENDERER=ON -DEAMMA_NATIVE_HOOKS=ON -DEAMMA_NATIVE_CENSUS=OFF"
if /i "%~1"=="census" set "NATIVE_ARG=-DEAMMA_NATIVE_RENDERER=ON -DEAMMA_NATIVE_HOOKS=OFF -DEAMMA_NATIVE_CENSUS=ON"
if /i "%~1"=="nonative" set "NATIVE_ARG=-DEAMMA_NATIVE_RENDERER=OFF"
"%SDK%\bin\rexglue.exe" codegen eamma_manifest.toml || exit /b 1
cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH="%SDK%" -Drexglue_DIR="%SDK%\lib\cmake\rexglue" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG" %NATIVE_ARG% || exit /b 1
cmake --build --preset win-amd64-relwithdebinfo || exit /b 1
set "OUT=out\build\win-amd64-relwithdebinfo"
for %%f in (rexruntimerd.dll rexgpu-xenosrd.dll TracyClientrd.dll) do (
  copy /y "%SDK%\bin\%%f" "%OUT%\" >nul || exit /b 1
)
echo.
echo Built: %CD%\%OUT%\eamma.exe
