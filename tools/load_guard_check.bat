@echo off
setlocal
REM Compiles tools\load_guard_check.cpp (include\LoadGuardCore.h, the loading guard's rules: AMF does nothing before the
REM Main Menu opens or while the Loading Menu is up) with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-load-guard-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\load_guard_check.exe" "%~dp0load_guard_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\load_guard_check.exe" %*
