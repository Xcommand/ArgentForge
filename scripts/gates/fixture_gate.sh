#!/bin/sh
# Every parser shape the constructor has to keep byte-exact, in one command:
#   fixture_gate.sh            - untouched, add-and-remove, clear everything
# Runs the fixtures under scripts/fixtures, which are the shapes the corpus of
# real mod files on disk doesn't contain. They're written here rather than cut
# out of somebody's mod, so the folder can go public with the rest of the repo.
#
# MODS_SRC points at the folder holding the mods, the same as the corpus gate;
# without it the parts that read real root lumps and the manifest are skipped,
# because they're the parts that need someone's collection to exist.
mods="$MODS_SRC"
root=$(cd "$(dirname "$0")/../.." && pwd -W)
exe="$root/out/scratch/actor_roundtrip.exe"
fix="$root/scripts/fixtures"
tmp="$root/out/scratch"
man="$tmp/corpus_manifest.txt"
cd "$root" || exit 1

if [ ! -x "$exe" ]; then
	echo "want the harness built first: scripts/harness/build.bat"
	exit 1
fi

# The include line a new actor file gets, against the root-lump shapes
"$exe" --includes 2>&1 | grep -qa "all green, 0" || printf '%-28s includes NOT\n' "insert include"

# ...and against the root lumps real mods actually carry, which is where the
# shapes stop being invented
for f in "brutal_wolfen/DECORATE.txt" "old_vers/Penetration_v2_5/ZSCRIPT" "old_vers/Penetration_v2_6/ZSCRIPT" \
         "Penetration_mod/Penetration_v1_2/ZSCRIPT" "Project_Brutality-PB_Staging/DECORATE" \
         "Project_Brutality-PB_Staging/ZSCRIPT.zc" "Project_Brutality-PB_Staging/filter/chex/DECORATE"; do
	[ -n "$mods" ] && [ -f "$mods/$f" ] || continue
	"$exe" --includesweep "$mods/$f" 2>&1 | grep -qa "^pass" \
		|| printf '%-28s root lump NOT  %s\n' "insert include" "$f"
done

if [ -n "$mods" ] && [ -f "$man" ]; then
	# Which language a file is, with nothing but its own text to go on
	"$exe" --formatsweep "$man" "$mods" 2>&1 | grep -qa "fail=0" \
		|| printf '%-28s format NOT\n' "language sniff"

	# Where "Add to states" lands, for every actor of every real mod file. Every states
	# block that's there has to be found (missed), and 'packed' just counts how many of
	# them are written whole on the '{' line
	"$exe" --statesweep "$man" "$mods" > "$tmp/statesweep.txt" 2>&1
	grep -qa "outside=0 no-place=0 missed=0" "$tmp/statesweep.txt" \
		&& grep -qa "bad-tag=0" "$tmp/statesweep.txt" \
		|| printf '%-28s landing NOT\n' "add to states"
	grep -a "^MISSED\|^NO PLACE\|^BAD TAG" "$tmp/statesweep.txt" | sort -u | head -5
	tail -1 "$tmp/statesweep.txt"
fi

# A call added to the text, then a flag applied from the window's older copy of the
# actor: the line has to survive the write-back
for t in "5 EmptyStates" "10 SpaceStates" "16 NewlinedStates"; do
	set -- $t
	"$exe" "$fix/emptystates.dec" decorate --insert "$1" "$2" A_Look SLADETESTFLAG 2>&1 \
		| grep -qa "after re-reading keeps the call: yes" \
		|| printf '%-28s write-back NOT  %s\n' "add to states" "$2"
done

# A ZScript actor that has a state function and a states block wants the states
# block: a frame belongs in one and a statement in the other
"$exe" "$fix/statefunc.zs" zscript --call 1 Foo A_Chase 2>&1 | grep -qa "TNT1 A 0 A_Chase" \
	|| printf '%-28s prefers NOT  state function over states\n' "add to states"

# Blocks written whole on the '{' line: empty, with a state in them, and inside a
# one-line actor. Every one of them has to take the frame as a pure insert
for t in "emptystates.dec decorate 24 FullStates" "emptystates_shapes.dec decorate 13 GluedStates" \
         "emptystates_shapes.dec decorate 30 PackedStates"; do
	set -- $t
	"$exe" "$fix/$1" "$2" --call "$3" "$4" A_Look > "$tmp/packed_call.txt" 2>&1
	grep -qa "TNT1 A 0 A_Look()" "$tmp/packed_call.txt" \
		&& grep -qa "rest of file unchanged: yes" "$tmp/packed_call.txt" \
		|| printf '%-28s packed NOT  %s\n' "add to states" "$4"
done

# The frame an added action takes: by default nothing is drawn, and with the
# settings on it carries the last picture actually listed above it. A line of
# several frame letters counts as its last one, and the lines that borrow a
# picture ('####', '#') look further up instead of giving up
for t in "6 keep|MANA A 0" "6 keep+next|MANA B 0" "6 keep+next+tics=4|MANA B 4" \
         "6 -|TNT1 A 0" "5 keep+next+tics=4|TNT1 A 4" "8 keep+next+tics=4|MANA D 4" \
         "11 keep+next+tics=4|TNT1 A 4" "12 keep+next+tics=4|TNT1 A 4" \
         "14 keep+next+tics=4|MANA B 4" "15 keep+next+tics=4|TNT1 A 4" \
         "16 keep+next+tics=4|TNT1 A 4" "17 keep+next|EBLT L 0" "20 keep|EBLT K 0" \
         "20 keep+next+tics=4|EBLT L 4" "21 keep+next+tics=4|EBCD E 4" \
         "22 keep+next+tics=4|EBCD C 4" "23 keep+next+tics=4|MANA C 4" \
         "24 keep+next+tics=4|EBLT E 4" "25 keep+next|FRAM G 0" \
         "26 keep+next+tics=4|FRAM G 4"; do
	set -- $(echo "$t" | tr '|' ' ')
	line=$1
	style=$2
	shift 2
	want=$*
	[ "$style" = "-" ] && style=""
	"$exe" "$fix/frameabove.dec" decorate --call "$line" FrameAbove A_FaceTarget "" "$style" > "$tmp/frame.txt" 2>&1
	grep -qa "$want A_FaceTarget" "$tmp/frame.txt" \
		&& grep -qa "rest of file unchanged: yes" "$tmp/frame.txt" \
		|| printf '%-28s frame NOT  line %s %s, want %s\n' "add to states" "$line" "${style:-default}" "$want"
done

for f in indent_test.dec zscript_bases.txt decorate_spawner.txt place_test.dec place_test_crlf.dec \
         emptybody.dec mixedeol.dec oneline.zs sameline.zs edges.zs emptystates.dec \
         emptystates_shapes.dec statefunc.zs emptydefaults.dec emptydefaults.zs \
         commentblock.dec frameabove.dec; do
	file="$fix/$f"
	if [ ! -f "$file" ]; then
		printf '%-28s missing\n' "$f"
		continue
	fi

	# The language each file is written in, which is what the constructor works out
	# for itself; a .txt lump full of 'class' lines isn't DECORATE
	case "$f" in *.zs|*zscript_bases*) mode=zscript ;; *) mode=decorate ;; esac

	if "$exe" "$file" "$mode" 2>&1 | grep -qa "untouched round-trip: identical"; then
		printf '%-28s untouched ok\n' "$f"
	else
		printf '%-28s untouched NOT\n' "$f"
	fi

	for a in $(grep -aoP '\[\d+\] \K[^,]+' <("$exe" "$file" "$mode" 2>&1)); do
		"$exe" "$file" "$mode" --revert "$a" SLADETESTFLAG >/dev/null 2>&1 \
			|| printf '%-28s revert NOT  %s\n' "$f" "$a"
		"$exe" "$file" "$mode" --clear "$a" >/dev/null 2>&1 \
			|| printf '%-28s clear NOT %s\n' "$f" "$a"
	done
done
echo "gate done"
