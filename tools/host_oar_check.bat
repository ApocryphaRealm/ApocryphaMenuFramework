@echo off
setlocal
REM Compiles tools\host_oar_check.cpp (bridges\oar\source\HostLogic.h - AMF-Host-OAR's frame logic) against the bridge's
REM own Dear ImGui 1.89.6 (Open Animation Replacer's pin) and runs it. bridges\oar\build.bat must have run once.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-host-oar-check-build
if not exist "%OUT%" mkdir "%OUT%"
set VI=%~dp0..\bridges\oar\build\se\vcpkg_installed\x64-windows-static-md
cl /nologo /std:c++20 /EHsc /O2 /MD /W3 /DIMGUI_DEFINE_MATH_OPERATORS /I"%VI%\include" /I"%~dp0..\include" /Fo"%OUT%\\" /Fe"%OUT%\host_oar_check.exe" "%~dp0host_oar_check.cpp" /link /LIBPATH:"%VI%\lib" imgui.lib user32.lib imm32.lib psapi.lib >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\host_oar_check.exe" %*
