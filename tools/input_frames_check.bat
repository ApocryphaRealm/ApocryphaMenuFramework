@echo off
setlocal
REM Compiles tools\input_frames_check.cpp (include\InputFrames.h, the input drain's one-change-per-button-per-frame rule)
REM with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-input-frames-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\input_frames_check.exe" "%~dp0input_frames_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\input_frames_check.exe" %*
