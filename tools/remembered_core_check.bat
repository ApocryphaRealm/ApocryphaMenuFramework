@echo off
setlocal
REM Compiles tools\remembered_core_check.cpp (include\RememberedCore.h) with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-remembered-core-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /Fo"%OUT%\\" /Fe"%OUT%\remembered_core_check.exe" "%~dp0remembered_core_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\remembered_core_check.exe"
