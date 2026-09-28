@echo off
rem Build everything this checkout is configured for. Pass a target to build less:
rem   build.bat slade
rem cl.exe needs the compiler's own environment, so vcvars is called here too; VCVARS points at
rem it on a machine where Visual Studio sits somewhere else (see configure.bat).
if "%VCVARS%"=="" set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if "%CMAKE%"=="" set CMAKE=cmake
if "%NINJA%"=="" set NINJA=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja

call "%VCVARS%" || exit /b 1
chcp 65001 >nul
set PATH=%NINJA%;%PATH%
if "%1"=="" (
	"%CMAKE%" --build "%~dp0out\build\win-x64-release"
) else (
	"%CMAKE%" --build "%~dp0out\build\win-x64-release" --target %1
)
