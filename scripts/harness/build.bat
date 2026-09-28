@echo off
rem Builds the headless actor-parser check against the objects the real build
rem already produced, so it tests the same code that's in ArgentForge.exe.
rem Needs a finished SLADE build first (build.bat slade).
rem   scripts\harness\build.bat
rem The compiler's own setup script moves with each Visual Studio version, so
rem point VCVARS at wherever yours is if the one below isn't right.
if "%VCVARS%"=="" set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul

rem Everything hangs off the repo, which is two folders up from this file
pushd "%~dp0..\.."
set ROOT=%CD%
popd

set BIN=%ROOT%\out\scratch
set INCLUDE=%ROOT%\vcpkg_installed\x64-windows-static\include;%INCLUDE%

rem Objects drop into wherever we're standing, so stand where they belong
pushd "%BIN%"

cl /nologo /std:c++20 /utf-8 /EHsc /MT /FS ^
   /DSFML_STATIC /D__WXMSW__ /DwxUSE_GUI=1 ^
   /I"%ROOT%\src" /I"%ROOT%\src\.." /I"%ROOT%\src\.\Application" ^
   /I"%ROOT%\thirdparty\fmt\include" /I"%ROOT%\thirdparty\dumb" ^
   /Fe"%BIN%\actor_roundtrip.exe" ^
   "%ROOT%\scripts\harness\actor_roundtrip.cpp" ^
   "%ROOT%\scripts\harness\strutil_stubs.cpp" ^
   "%ROOT%\src\UI\Dialogs\ActorConstructor\ActorConstructor.cpp" ^
   "%ROOT%\out\build\win-x64-release\src\external\external.lib" ^
   /link /LIBPATH:"%ROOT%\vcpkg_installed\x64-windows-static\lib"

popd
