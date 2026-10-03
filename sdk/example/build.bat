@echo off
setlocal
REM Configure (the first run builds CommonLibSSE-NG and Dear ImGui through vcpkg - VCPKG_ROOT must be set) and build.
call "%~dp0find-msvc.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\relwithdebinfo-se-only\build.ninja (
	cmake --preset build-relwithdebinfo-se-only
	if errorlevel 1 exit /b 1
)
cmake --build build/relwithdebinfo-se-only
