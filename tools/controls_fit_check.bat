@echo off
setlocal
REM Compiles tools\controls_fit_check.cpp against AMF's own ImGui build (1.90.8 with the overlay patches, from the SE/AE
REM line's vcpkg tree - configure.bat must have run once) and runs it headless. See the .cpp for what it proves.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-controls-fit-check-build
if not exist "%OUT%" mkdir "%OUT%"
set VI=%~dp0..\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /I"%VI%\include" /Fo"%OUT%\\" /Fe"%OUT%\controls_fit_check.exe" "%~dp0controls_fit_check.cpp" "%VI%\lib\imgui.lib" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\controls_fit_check.exe" %*
