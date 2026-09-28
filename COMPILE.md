## Linux

### Mandatory build-time requirements

* C++17 compiler (e.g. g++ >= 8.x)
* bzip2 library
* ftgl, an OpenGL font managing library
* mpg123 library >= 1.28.1
* OpenGL
* SFML
* wxWidgets >= 3.3.1 (it may build with lesser, but that is not recommended due
  to known bugs)
* zlib
* libwebp

### Optional build-time requirements

* Fluidsynth (deactivate with `-DNO_FLUIDSYNTH=ON`)
* Lua (deactivate with `-DNO_LUA=ON`)

### Additional configure switches for cmake

* `-DNO_COTIRE=ON`: disable the use of precompiled headers
* `-DNO_WEBVIEW=ON`: use if your wxWidgets build has no wxWebview or if not desired
* `-DWX_GTK3=OFF`: use if your wxWidgets build is using the wxGTK2 backend (there is no autodetection at this point)

## Windows

SLADE can be built on Windows using [Visual Studio](https://visualstudio.microsoft.com/) 2019+ (a free 'community' edition is available which works fine) and [vcpkg](https://docs.microsoft.com/en-us/cpp/build/vcpkg?view=vs-2019) for handling the required external libraries.

### Required vcpkg libraries

* lua
* mpg123
* opengl
* sfml
* wxwidgets
* libwebp

The above libraries are required for building SLADE on windows. Note that you'll most likely want to use the `x64-windows-static` triplet when installing them, eg.

```
.\vcpkg install <libraries> --triplet x64-windows-static
```

### Building here on Windows

This fork has two scripts at the top of the repository, and they are the shortest way through:

```
configure.bat
build.bat
```

`configure.bat` sets up a Ninja build in `out/build/win-x64-release` and `build.bat` runs it; the
program lands in `dist\` as `ArgentForge.exe` next to its `slade.pk3`. Both take a target:
`build.bat slade` builds just the program.

They look for Visual Studio and its vcpkg in the usual place. If yours sits elsewhere, set these
before running them and they will be used instead:

* `VCVARS` - full path to `vcvars64.bat`
* `VCPKG_TOOLCHAIN` - full path to `vcpkg.cmake`
* `NINJA`, `CMAKE` - folders or commands for those two

The gates under `scripts/gates/` need the headless parser build first: `scripts\harness\build.bat`,
which links against the objects `build.bat` already left behind.

### Packing a portable build

Once `build.bat` has run:

```
sh scripts/release/portable_zip.sh
```

It takes the version from `src/Application/App.cpp`, checks it against the version the exe already
carries in its file properties, and writes `dist/release/argentforge_<version>_win64_portable.zip`:
the program, `slade.pk3`, the license and a page of README. Windows needs nothing else with it, the
libraries are linked in. If 7-Zip isn't installed in `C:\Program Files\7-Zip`, point at it with
`SEVENZ=C:\path\to\7z.exe`.
