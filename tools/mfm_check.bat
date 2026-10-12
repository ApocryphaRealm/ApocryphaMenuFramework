@echo off
setlocal
REM Compiles tools\mfm_check.cpp (include\MfmCore.h, the Mod Function Menu entries' reader, resolver and guarded call)
REM with MSVC and runs it. Optional argument: a folder holding ccld_ModFunctionMenu\{Mod,Config} (a released MFM's
REM SKSE\Plugins folder), whose real entries are read too.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-mfm-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\mfm_check.exe" "%~dp0mfm_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\mfm_check.exe" %*
