@echo off
setlocal
REM Compiles tools\flick_slots_check.cpp (the FLICK table AMF answers: API 7, 303 slots, version 5's 259 as its prefix but
REM slot 255, VSliderButton kept the stub, every v6/v7 slot answered; FlickToolPath.h's nested groups - 2.3.0) with MSVC
REM and runs it against source\FlickHost.cpp.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-flick-slots-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /Fo"%OUT%\\" /Fe"%OUT%\flick_slots_check.exe" "%~dp0flick_slots_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\flick_slots_check.exe" "%~dp0..\source\FlickHost.cpp"
