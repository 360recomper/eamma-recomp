@echo off
rem Starts the EA Sports MMA recomp (Vulkan). Log goes to play.log.
rem Files under mods\ replace same-path files in assets\ (add --mods_enabled=false to skip).
set "ROOT=%~dp0"
set "ROOT=%ROOT:\=/%"
cd /d "%~dp0out\build\win-amd64-relwithdebinfo"
.\eamma.exe --game_data_root="%ROOT%assets" --user_data_root="%ROOT%userdata" --mods_root="%ROOT%mods" --gpu_backend=vulkan ^
  --log_file="%ROOT%play.log" --log_level=info %*
