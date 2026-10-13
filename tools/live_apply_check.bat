@echo off
setlocal
REM Compiles tools\live_apply_check.cpp (include\LiveApplyCore.h, when a change on a converted page is applied live)
REM with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-live-apply-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\live_apply_check.exe" "%~dp0live_apply_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\live_apply_check.exe" %*
