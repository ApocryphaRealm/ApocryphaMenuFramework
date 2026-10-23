@echo off
setlocal
REM Compiles tools\keyboard_theme_render.cpp against AMF's own ImGui build (1.90.8 with the overlay patches, from the SE/AE
REM line's vcpkg tree - configure.bat must have run once) and renders the on-screen keyboard under every shipped theme,
REM before and after the 2.3.0 key-shape fix. Argument: the output folder (default: %TEMP%\amf-keyboard-renders).
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set OUT=%TEMP%\amf-keyboard-render-build
if not exist "%OUT%" mkdir "%OUT%"
set VI=%~dp0..\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /I"%VI%\include" /Fo"%OUT%\\" /Fe"%OUT%\keyboard_theme_render.exe" "%~dp0keyboard_theme_render.cpp" "%VI%\lib\imgui.lib" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
set DEST=%~1
if "%DEST%"=="" set DEST=%TEMP%\amf-keyboard-renders
"%OUT%\keyboard_theme_render.exe" "%~dp0..\dist\SKSE\Plugins\ApocryphaMenuFramework" "%DEST%"
