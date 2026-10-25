@echo off
setlocal
REM Compiles tools\null_guard_check.cpp (include\utils\NullGuard.h - AMF_REQUIRE, AMF_REQUIRE_VOID, AMF_OR, amf::Str,
REM amf::Call) with MSVC against a stand-in logger and runs it. No arguments.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-null-guard-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++latest /EHsc /O2 /W4 /utf-8 /I"%~dp0null_guard_check_stub" /I"%~dp0..\include" /Fo"%OUT%\\" /Fe"%OUT%\null_guard_check.exe" "%~dp0null_guard_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\null_guard_check.exe" %*
