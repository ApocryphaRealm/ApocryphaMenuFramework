@echo off
setlocal
REM Compiles tools\window_geometry_check.cpp (include\WindowGeometryCore.h, when the key-opened window's saved geometry is
REM applied and saved - never against a 0x0 or 8x8 placeholder display, 2.3.0) with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-window-geometry-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\window_geometry_check.exe" "%~dp0window_geometry_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\window_geometry_check.exe" %*
