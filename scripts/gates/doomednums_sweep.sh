#!/bin/sh
# Headless gate for the DoomEdNums writer: every *MAPINFO* text file on the mod
# disk, run through registering one more actor and taking it back out. Adding has
# to be a pure insertion - the rest of the lump can't move - and removing what was
# added has to leave the file byte for byte as it was read.
#
# Needs the harness built first (scripts/harness/build.bat).
#
#   MODS_SRC="/path/to/your/mods" bash scripts/gates/doomednums_sweep.sh
#
# The whole tree is walked rather than a list of folders, because a folder left out
# of a gate is a folder the gate has never looked at
mods="$MODS_SRC"
root=$(cd "$(dirname "$0")/../.." && pwd -W)
exe="$root/out/scratch/actor_roundtrip.exe"
list="$root/out/scratch/doomednums_files.txt"
out="$root/out/scratch/doomednums_result.txt"

if [ ! -x "$exe" ] || [ -z "$mods" ] || [ ! -d "$mods" ]; then
	echo "want the harness and MODS_SRC pointing at the mods to read:"
	echo "      scripts/harness/build.bat"
	echo "      MODS_SRC=\"\$mods\" bash scripts/gates/doomednums_sweep.sh"
	exit 1
fi

# The names SLADE writes into, in any case and with or without an extension, minus
# the compiled and packaged things that only happen to share one
find "$mods" -type f -iname "*mapinfo*" \
	! -name "*.obj" ! -name "*.exe" ! -name "*.dll" ! -name "*.lib" \
	! -name "*.o" ! -name "*.h" ! -name "*.hpp" ! -name "*.cpp" \
	! -name "*.pk3" ! -name "*.zip" ! -name "*.wad" -print0 2>/dev/null > "$list"

pass=0; fail=0; skip=0
: > "$out"

# Read through the list rather than a pipe, so the counters are still here after
while IFS= read -r -d '' f; do
	# A file with a nought byte in it isn't a script, so it says nothing about the writer
	if grep -qaP '\0' "$f"; then
		skip=$((skip+1))
		continue
	fi

	line=$("$exe" --doomednums-sweep "$f" 2>&1)
	if [ $? -eq 0 ]; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
		echo "FAIL $f :: $line" >> "$out"
	fi
done < "$list"

echo "doomednums sweep: pass=$pass fail=$fail skipped=$skip"
[ "$fail" = 0 ]
