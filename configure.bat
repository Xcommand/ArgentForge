@echo off
rem Configure this checkout for a Windows build. The paths that belong to one machine can all
rem be overridden from the environment, so this runs outside this folder on any checkout:
rem   VCVARS           the vcvars64.bat to call
rem   VCPKG_TOOLCHAIN  vcpkg's cmake toolchain file
rem   NINJA            a folder holding ninja.exe
rem   CMAKE            cmake.exe, or just cmake if it's already on PATH
rem Dependencies are expected to sit in vcpkg_installed already, because
rem VCPKG_MANIFEST_INSTALL is off on purpose: a configure that also rebuilds wxWidgets and
rem friends every time is not what a build gate wants to wait for.
if "%VCVARS%"=="" set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if "%VCPKG_TOOLCHAIN%"=="" set VCPKG_TOOLCHAIN=C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/vcpkg/scripts/buildsystems/vcpkg.cmake
if "%NINJA%"=="" set NINJA=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
if "%CMAKE%"=="" set CMAKE=cmake

call "%VCVARS%" || exit /b 1
chcp 65001 >nul
set PATH=%NINJA%;%PATH%

"%CMAKE%" ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DVCPKG_INSTALLED_DIR:PATH="%~dp0vcpkg_installed" ^
  -DVCPKG_MANIFEST_INSTALL=OFF ^
  -DCMAKE_TOOLCHAIN_FILE:FILEPATH="%VCPKG_TOOLCHAIN:\=/%" ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE ^
  -S "%~dp0." ^
  -B "%~dp0out\build\win-x64-release" ^
  -G Ninja
