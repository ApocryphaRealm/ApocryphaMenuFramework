@echo off
setlocal
REM Compiles tools\mcm_sort_check.cpp with MSVC and runs it on the names given (see the .cpp).
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-mcm-sort-check
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /Fo"%OUT%\\" /Fe"%OUT%\mcm_sort_check.exe" "%~dp0mcm_sort_check.cpp" >nul || exit /b 1
"%OUT%\mcm_sort_check.exe" %*
