@echo off
setlocal
REM Compiles tools\toml_check.cpp (include\TomlCore.h - the TOML pages' reader and line-preserving writer; include\GsoCore.h -
REM Game Settings Override's special case; include\RefreshCore.h - which page a declared reload belongs to) with MSVC and
REM runs it. Optional arguments: folders whose .toml files (any depth) are each read and written back in memory (read only).
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-toml-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /Fo"%OUT%\\" /Fe"%OUT%\toml_check.exe" "%~dp0toml_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\toml_check.exe" %*
