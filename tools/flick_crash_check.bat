@echo off
setlocal
REM Compiles tools\flick_crash_check.cpp (include\FlickCrashCore.h, the FLICK crash notice: the last-session marker, which
REM crash log is read, the plain "FUCK.dll" search, which mods are offered - 2.2.1) with MSVC and runs it.
REM Optional argument: a real crash log that names FUCK.dll.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-flick-crash-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\flick_crash_check.exe" "%~dp0flick_crash_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\flick_crash_check.exe" %*
