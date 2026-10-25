@echo off
setlocal
REM Compiles tools\null_selftest_check.cpp with AMF's GENERATED source\cimgui\cimgui.cpp against AMF's own ImGui build (1.90.8
REM with the overlay patches, from the SE/AE line's vcpkg tree - configure.bat must have run once) and runs the null
REM self-test's cimgui cases headless. See the .cpp for what it proves.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-null-selftest-check-build
if not exist "%OUT%" mkdir "%OUT%"
set VI=%~dp0..\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /DCIMGUI_NO_EXPORT /I"%VI%\include" /I"%~dp0..\include" /Fo"%OUT%\\" /Fe"%OUT%\null_selftest_check.exe" "%~dp0null_selftest_check.cpp" "%~dp0..\source\cimgui\cimgui.cpp" "%VI%\lib\imgui.lib" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\null_selftest_check.exe" %*
