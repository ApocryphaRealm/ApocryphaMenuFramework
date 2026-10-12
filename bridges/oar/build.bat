@echo off
setlocal
REM Configures and builds AMF-Host-OAR for both game lines: build\se (SE 1.5.97 / AE 1.6) and build\17 (Skyrim 1.7.x).
REM The first configure of each makes vcpkg build CommonLibSSE-NG and Dear ImGui 1.89.6, so it takes a while.
REM Requires VCPKG_ROOT; Visual Studio and its CMake / Ninja are found by the repository's find-msvc.bat, which also
REM enters the shared build environment (MODS_BUILD_ENV) when it is set.

call "%~dp0..\..\find-msvc.bat"
if errorlevel 1 exit /b 1

if "%VCPKG_ROOT%"=="" (
	echo VCPKG_ROOT is not set. Point it at your vcpkg checkout and try again.
	exit /b 1
)

cd /d "%~dp0"
cmake --preset se
if errorlevel 1 exit /b 1
cmake --build build/se
if errorlevel 1 exit /b 1
cmake --preset 17
if errorlevel 1 exit /b 1
cmake --build build/17
if errorlevel 1 exit /b 1
exit /b 0
