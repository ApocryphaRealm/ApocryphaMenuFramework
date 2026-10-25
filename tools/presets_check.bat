@echo off
setlocal
REM Compiles tools\presets_check.cpp (include\PresetsCore.h - the settings presets' file format, selection, comparison and
REM names) with MSVC and runs it. Optional arguments: folders of preset .json files (a RememberedSettings or Presets folder),
REM each read and written back in memory - read only.
REM nlohmann/json comes from the SE/AE line's vcpkg tree, so configure.bat must have run once.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-presets-check-build
if not exist "%OUT%" mkdir "%OUT%"
set INC=%~dp0..\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md\include
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /I"%INC%" /Fo"%OUT%\\" /Fe"%OUT%\presets_check.exe" "%~dp0presets_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\presets_check.exe" %*
