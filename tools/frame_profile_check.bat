@echo off
setlocal
REM Compiles tools\frame_profile_check.cpp (include\FrameProfileCore.h, which frames after an AMF opening are measured
REM and logged - the slow first open of the 2026-10-16 to-do entry, 2.3.0) with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-frame-profile-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\frame_profile_check.exe" "%~dp0frame_profile_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\frame_profile_check.exe" %*
