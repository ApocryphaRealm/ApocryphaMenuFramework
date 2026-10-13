@echo off
setlocal
REM Compiles tools\gso_check.cpp (include\GsoCore.h, the Game Settings Override page's TOML reader and writer and its type
REM mapping) with MSVC and runs it. Optional argument: a folder of GSO .toml files, each read and written back in memory
REM (read only).
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-gso-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /Fo"%OUT%\\" /Fe"%OUT%\gso_check.exe" "%~dp0gso_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\gso_check.exe" %*
