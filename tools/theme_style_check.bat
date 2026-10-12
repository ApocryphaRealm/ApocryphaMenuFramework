@echo off
setlocal
REM Compiles tools\theme_style_check.cpp (include\ThemeStyle.h, the style theme::Apply builds) against the build's own
REM Dear ImGui (vcpkg, configure the relwithdebinfo-17 preset first) with MSVC and runs it over the shipped themes.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set VCI=%~dp0..\build\relwithdebinfo-17\vcpkg_installed\x64-windows-static-md
if not exist "%VCI%\lib\imgui.lib" (echo theme_style_check: %VCI%\lib\imgui.lib not found - configure the build first & exit /b 1)
set OUT=%TEMP%\amf-theme-style-check-build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /I"%VCI%\include" /Fo"%OUT%\\" /Fe"%OUT%\theme_style_check.exe" "%~dp0theme_style_check.cpp" /link /LIBPATH:"%VCI%\lib" imgui.lib user32.lib imm32.lib >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\theme_style_check.exe" "%~dp0..\dist\SKSE\Plugins\ApocryphaMenuFramework\themes"
