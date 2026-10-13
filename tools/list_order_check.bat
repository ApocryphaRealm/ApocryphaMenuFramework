@echo off
setlocal
REM Compiles tools\list_order_check.cpp with source\Personalization.cpp (the mod list's order) and include\ListCore.h, and
REM runs it. Optional arguments: a file of registered mod names (one per line) and a User.ini - see the .cpp.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-list-order-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /I"%~dp0list_order_check_stub" /I"%~dp0..\include" /Fo"%OUT%\\" /Fe"%OUT%\list_order_check.exe" "%~dp0list_order_check.cpp" "%~dp0..\source\Personalization.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\list_order_check.exe" %*
