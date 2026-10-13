@echo off
setlocal
REM Compiles tools\text_input_check.cpp (include\TextInputCore.h - UTF-8 decoding, UTF-16, IME window scaling and the
REM engine-CharEvent / WM_CHAR arbiter) with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-text-input-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /Fo"%OUT%\\" /Fe"%OUT%\text_input_check.exe" "%~dp0text_input_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\text_input_check.exe" %*
