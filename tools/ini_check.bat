@echo off
setlocal
REM Compiles tools\ini_check.cpp (include\IniCore.h, the INI pages' shown labels and own-page match) with MSVC and runs it.
REM Optional argument: a mods folder holding "Back Pocket For Controller" and "SSE Display Tweaks" (default
REM D:\Modlists\Njordlinger\mods), whose INIs are read, never written.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-ini-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\ini_check.exe" "%~dp0ini_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\ini_check.exe" %*
