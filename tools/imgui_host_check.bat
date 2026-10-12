@echo off
setlocal
REM Compiles tools\imgui_host_check.cpp (include\HostCore.h - the other mods' menu keys, the SendInput records and the
REM frame-time statistics) with MSVC and runs it. Optional arguments: a Mod Organizer instance's mods folder and its
REM overwrite folder, whose real Open Animation Replacer, IED, Community Shaders and OptiScaler configs are read too.
REM nlohmann/json comes from the SE/AE line's vcpkg tree, so configure.bat must have run once.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-imgui-host-check-build
if not exist "%OUT%" mkdir "%OUT%"
set INC=%~dp0..\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md\include
cl /nologo /std:c++20 /EHsc /O2 /W4 /I"%INC%" /Fo"%OUT%\\" /Fe"%OUT%\imgui_host_check.exe" "%~dp0imgui_host_check.cpp" user32.lib >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\imgui_host_check.exe" %*
