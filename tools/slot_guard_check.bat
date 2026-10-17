@echo off
setlocal
REM Compiles tools\slot_guard_check.cpp (no game, no VM) and runs it: the 2.2.2 slot guard refuses the 2026-10-17 crash value.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-slot-guard-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /Fo"%OUT%\\" /Fe"%OUT%\slot_guard_check.exe" "%~dp0slot_guard_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\slot_guard_check.exe" %*
