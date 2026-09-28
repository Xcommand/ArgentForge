#!/bin/sh
# Packs the portable Windows build for a GitHub release:
#   portable_zip.sh
# It takes whatever dist/ already holds, so run build.bat first. The output is a
# zip with one folder in it, because the program is that folder: nothing installs,
# nothing registers, and no file is written beside the exe.
#
# 7z path can be overridden with SEVENZ=/path/7z.exe for a machine where it isn't
# installed next to Program Files.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd -W)
sevenz="${SEVENZ:-C:/Program Files/7-Zip/7z.exe}"
stage="$root/dist/release/stage"
out="$root/dist/release"

exe="$root/dist/ArgentForge.exe"
pk3="$root/dist/slade.pk3"
[ -f "$exe" ] || { echo "no $exe, run build.bat"; exit 1; }
[ -f "$pk3" ] || { echo "no $pk3, run build.bat"; exit 1; }
[ -x "$sevenz" ] || [ -f "$sevenz" ] || { echo "no 7z at $sevenz (set SEVENZ=...)"; exit 1; }

# The version comes from the same line the program prints in Help->About, so a
# release can't be named for a build that says something else
ver=$(sed -n 's/^Version version_num{ *\([0-9]*\), *\([0-9]*\), *\([0-9]*\).*/\1.\2.\3/p' \
	"$root/src/Application/App.cpp" | head -1)
[ -n "$ver" ] || { echo "couldn't read version_num from App.cpp"; exit 1; }

# The exe's own file properties have to agree with the name of the zip
rc=$(sed -n 's/^#define SLADE_VERSION_STR "\([0-9.]*\)".*/\1/p' "$root/msvc/SLADE.rc" | head -1)
[ "$rc" = "$ver" ] || { echo "msvc/SLADE.rc says $rc but App.cpp says $ver"; exit 1; }

name="argentforge_${ver}_win64_portable"
rm -rf "$stage"
mkdir -p "$stage/ArgentForge"
cp "$exe" "$pk3" "$stage/ArgentForge/"
cp "$root/LICENSE" "$stage/ArgentForge/LICENSE.txt"

cat >"$stage/ArgentForge/README.txt" <<TXT
Argent Forge ${ver}
==================

A Doom editor: a fork of SLADE3, which Simon Judd wrote. It opens WAD, PK3 and
the other formats those games use, and edits their graphics, textures, maps and
scripts. This build adds a visual actor constructor for DECORATE and ZScript, a
sprite preview in the code editor, painting tools for the picture editor, and
two colour schemes. The full list is CHANGES.md in the repository.

Run
---
Unzip the folder anywhere and start ArgentForge.exe. Nothing is installed, and
no file is written next to the exe. Needs 64-bit Windows.

Settings live in %APPDATA%\SLADE3, the folder SLADE3 itself uses, so both
editors read and write one file. Each program writes only the options it knows
about, which means running plain SLADE drops the lines this fork added, and they
come back from their defaults the next time Argent Forge starts. Delete that
folder to start over.

The start page and the crash dialog use the WebView2 runtime that ships with
Edge. Where it isn't installed, the start page falls back to a simpler text
version and everything else works as usual.

License
-------
GPL-2.0-only, same as SLADE3. Source, issues and the changelog:
https://github.com/Xcommand/ArgentForge
TXT

cd "$stage"
rm -f "$out/$name.zip"
"$sevenz" a -tzip -mx=9 "$out/$name.zip" ArgentForge >/dev/null
cd "$out"

printf '%s\n' "$name.zip"
"$sevenz" l "$name.zip" | sed -n '/Name/,$p'
printf 'size: %s bytes\n\n' "$(wc -c <"$name.zip")"
echo "to publish: github.com/Xcommand/ArgentForge -> Releases -> Draft a new release,"
echo "tag it v$ver, then drag $name.zip into the assets box"
