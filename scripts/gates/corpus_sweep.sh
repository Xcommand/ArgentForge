#!/bin/sh
# Headless corpus gate for the actor constructor: the whole stack of real mod
# sources on disk, run through the parser with nothing written back to them.
# Needs the harness built first (scripts/harness/build.bat) and the manifest
# listed (scripts/gates/corpus_list.js).
#
#   corpus_sweep.sh untouched        - every file must come back byte-identical
#   corpus_sweep.sh revert [n]       - add a flag and take it off again for up to n
#                                      actors per file; the file must not change
#   corpus_sweep.sh propcycle [n]    - same for one property in the defaults block
#   corpus_sweep.sh clear [n]        - take every entry out of one actor: what's left
#                                      has to be the file as it was read, or the block
#                                      dropped with the last line the constructor added
#   corpus_sweep.sh numbers          - every actor's thing number, against where its
#                                      own line holds one (DECORATE at the end of the
#                                      header, ZScript nowhere in it)
#
# The mod sources are named by MODS_SRC rather than guessed from where this
# checkout sits, since a gate that only ever passes against one person's disk
# proves nothing
mods="$MODS_SRC"
root=$(cd "$(dirname "$0")/../.." && pwd -W)
exe="$root/out/scratch/actor_roundtrip.exe"
man="$root/out/scratch/corpus_manifest.txt"
out="$root/out/scratch/corpus_result.txt"

if [ ! -x "$exe" ] || [ ! -f "$man" ] || [ -z "$mods" ] || [ ! -d "$mods" ]; then
	echo "want the harness, a manifest, and MODS_SRC pointing at the mods to read:"
	echo "      scripts/harness/build.bat"
	echo "      node scripts/gates/corpus_list.js \"\$mods\""
	echo "      MODS_SRC=\"\$mods\" bash scripts/gates/corpus_sweep.sh untouched"
	exit 1
fi

cd "$mods" || exit 1
: > "$out"

# Say up front how much the manifest holds, so a result can be read against it: a
# pass line that covers fewer files than this one isn't a clean sweep
want=$(grep -c $'\t' "$man")
echo "manifest rows: $want"

# The number sweep reads the whole manifest itself, so it stays in one process
if [ "$1" = numbers ]; then
	cd "$mods" || exit 1
	"$exe" --numbersweep "$man" "."
	exit $?
fi

if [ "$1" = untouched ]; then
	ok=0; bad=0
	while IFS=$'\t' read -r mode f; do
		if [ -z "$f" ]; then continue; fi
		if "$exe" "$f" "$mode" 2>&1 | grep -qa "untouched round-trip: identical"; then
			ok=$((ok+1))
		else
			bad=$((bad+1)); echo "NOT-IDENTICAL [$mode] $f" >> "$out"
		fi
	done < "$man"
	echo "untouched: identical=$ok not=$bad of $want"
	if [ "$bad" -ne 0 ] || [ "$ok" -ne "$want" ]; then exit 1; fi
	exit 0
fi

# What gets done to one actor, and what the line has to say when it went clean
case "$1" in
	revert)    op="--revert"; extra="SLADETESTFLAG"; word="CHANGED" ;;
	propcycle) op="--propcycle"; extra=""; word="PROPERTY LEFT" ;;
	clear)     op="--clear"; extra=""; word="NOT BACK" ;;
	*)         echo "want untouched, revert, propcycle or clear"; exit 1 ;;
esac

limit=${2:-2}
pass=0; fail=0; files=0
while IFS=$'\t' read -r mode f; do
	if [ -z "$f" ]; then continue; fi
	files=$((files+1))
	n=0
	for a in $(grep -aoP '\[\d+\] \K[^,]+' <("$exe" "$f" "$mode" 2>&1)); do
		n=$((n+1)); [ "$n" -gt "$limit" ] && break
		if "$exe" "$f" "$mode" $op "$a" $extra >/dev/null 2>&1; then
			pass=$((pass+1))
		else
			fail=$((fail+1)); echo "$word [$mode] $f :: $a" >> "$out"
		fi
	done
done < "$man"
echo "$1: pass=$pass fail=$fail over $files of $want files"
