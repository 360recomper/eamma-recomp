@echo off
rem Play test of the native renderer: 1440p internal resolution, coverage ledger on (the [cover] lines in
rem play.log tell the next session which shaders and modes were seen). Extra arguments are passed on.
call "%~dp0play.bat" --eamma_renderer=native --eamma_native_scale=2 --eamma_native_coverage=true %*
