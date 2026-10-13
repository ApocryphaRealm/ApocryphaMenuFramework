@echo off
setlocal
REM Compiles tools\glyph_coverage_check.cpp (include\GlyphCore.h - the glyph ranges, the fallback face, the atlas fit and the
REM on-demand path BuildFonts runs) against the ImGui 1.90.8 library of the SE/AE line's vcpkg tree and runs it. configure.bat
REM must have run once. Set GLYPH_CHECK_VERBOSE=1 to print every check and each atlas's size.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-glyph-coverage-check-build
if not exist "%OUT%" mkdir "%OUT%"
set VI=%~dp0..\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md
cl /nologo /std:c++20 /EHsc /O2 /MD /utf-8 /I"%VI%\include" /Fo"%OUT%\\" /Fe"%OUT%\glyph_coverage_check.exe" "%~dp0glyph_coverage_check.cpp" /link /LIBPATH:"%VI%\lib" imgui.lib >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\glyph_coverage_check.exe"
