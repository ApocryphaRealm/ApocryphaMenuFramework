@echo off
setlocal
REM Compiles tools\keyboard_layout_check.cpp (include\KeyboardLayouts.h - the on-screen keyboard's eleven layouts, Korean
REM syllable composition, the kana voicing marks and the UTF-8 helpers, 2.3.0) with MSVC and runs it.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-keyboard-layout-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /Fo"%OUT%\\" /Fe"%OUT%\keyboard_layout_check.exe" "%~dp0keyboard_layout_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\keyboard_layout_check.exe" %*
