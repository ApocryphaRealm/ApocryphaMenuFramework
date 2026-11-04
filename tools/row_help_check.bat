@echo off
setlocal
REM Compiles row_help_check.cpp against the ImGui build of AMF (1.90.8 with the overlay patches, from the SE-AE vcpkg
REM tree - configure.bat must have run once) and runs it headless (2.4.0).
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-row-help-check-build
if not exist "%OUT%" mkdir "%OUT%"
set VI=%~dp0..\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /I"%VI%\include" /Fo"%OUT%\\" /Fe"%OUT%\row_help_check.exe" "%~dp0row_help_check.cpp" "%VI%\lib\imgui.lib" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\row_help_check.exe" %*
