@echo off
setlocal
REM Compiles tools\flick_route_check.cpp (include\FlickRouteCore.h, where a FLICK mod goes: the player's choice, else a mod
REM with FLICK windows to the real FLICK when it is installed, else this menu - 2.2.1) with MSVC and runs it.
REM Optional argument: a folder of FLICK client DLLs to read for FLICK's IWindow class (see the .cpp).
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-flick-route-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\flick_route_check.exe" "%~dp0flick_route_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\flick_route_check.exe" %*
