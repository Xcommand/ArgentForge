# Reading a mod's code without hunting

A mod is a hundred small files, and the interesting part of any one of them is written somewhere
else: the actor a state jumps to, the sound a call plays, the sprite a frame line names, the cvar an
`if` reads. Right-click the name and the menu offers where it is defined, in this archive, with the
file named in brackets:

```
Go to VR_Rocket (ZScript, Projectiles.zs)
Go to leech/idle (SNDINFO sound, SNDINFO.guns)
Go to sv_aircontrol (CVARINFO cvar, CVARINFO)
Go to LECH (sprite, 26 frames, LECHAO)
```

Pick one and the file opens there. If the definition is in the file you are already reading, the
view just moves to the line and the name is selected.

## What it answers

* Classes and actors, in ZScript and DECORATE, including the parent in `class Foo : Bar` and what
  `replaces X` points at.
* State labels, the bare word in a frame line or a `goto`. A label the actor only inherits is
  found in the class that writes it.
* Functions the mod writes: a class's own `function` or DECORATE's `action`, and one written at
  file scope or inside a `namespace`. From wherever they are called. A routine the parent writes is
  found in the file that writes the parent, and comes before a global of the same name.
* Decals and the rest of GLDEFS: `decal`, `decalmask`, `pulverize`, `texture`, `material`,
  `glprojector`, `fading`, `particleeffect`, written bare or in quotes, in a `GLDEFS` or a
  `DECALDEF` file.
* ZScript's own global definitions: `particledef`, `particlegroup`, `texturedparticle`.
* Sounds: the alias a `SNDINFO` line writes, the file that line quotes, and the audio file itself.
* Cvars from `CVARINFO`, levels from `MAPINFO` (both `map MAP01` and its `lookup` name), and
  `LANGUAGE` keys, whether the file writes them as `NAME = "text";` or as a spreadsheet. A key is
  found by its own name, so the dollar a caller writes in front of it (`"$OB_MPROCKET"`) doesn't
  hide it.
* Sprites, flats, textures and patches answer by the file they are drawn from, however long the mod
  wrote its name: the eight-letter limit belongs to a WAD, not to a pk3. A sprite answers by its
  four letters and by a whole frame name, in both spellings a mod uses (`SNIPA1.png` and
  `sprites/snip/a1.png`), and the frame that opens is the one the engine would draw: rotation 0, or
  1 if there is no 0, from `sprites` before anywhere else. A brightmap never answers for the picture
  it brightens. A map answers by its own folder. Opening a map takes the back key with it until you
  click a text again.

A named random pool (`random[VR_WeaponRNG]`) is not a definition: the engine makes one up the first
time a mod writes its name (`m_random.cpp`), so no file declares it and there is nowhere to go.

## The order of the answers

Where a name has more than one definition, the menu lists them in the order the engine reads them,
so the first answer is the one the game uses. The two kinds of name disagree on purpose:

* Classes are read ZScript before DECORATE, and the first write of a name wins: a second class of
  that name is an error the engine reports, not an override.
* A sound, a cvar, a level or a translation string is overwritten by a later file, so the last read
  is what the game ends up with and that is the one offered first.

A word ranks by the file it was written in: `soldier.attack` in a `SNDINFO` is a sound name before
it is anything else, and in a script a class name leads.

## Names as they are written

`weapons/sshoto` and `soldier.hitmonk1` are one name each, not two: the word under the pointer grows
over the dots and slashes that stand between its parts, and inside a quoted run it takes the whole
run, which is how `A_StartSound("leech/idle", 1)` reaches the line that defines that sound. If the
whole name has no answer, its halves are asked for as well, so a call written as `Scope::Helper` or
a file named `sounds/weapons/sshoto.ogg` still lands somewhere.

## Going back

A jump remembers where you were. `Alt+Left` and `Alt+Right` walk that trail, the mouse's side
buttons do the same, and the right-click menu lists the jump at its top: `Back to Sniper.zs`. It
works after a jump that landed on a picture, not just between texts, and a place whose file has
since been deleted is skipped rather than blocking the button. The trail holds a hundred jumps;
older ones fall off.

Both keys are ordinary keybinds, listed under **Text Editor** in the input settings, so they can be
moved to other keys.

## When it says nothing

Over a file with names in it the menu always answers, even when the answer is none: a grey line says
how many classes, names and files were read. That is the difference between 'this mod has no such
thing' and 'the archive was never looked at', and it is the first thing to check when a jump you
expected is missing. On a large mod the line reads something like 987 classes, 2010 names and 11448
files.

The walk asks the archive's own listing how big each file is, so it never loads a picture to find
out it isn't text. Only the files it does read are parsed, and a file that hasn't changed size since
the last right-click isn't read again.
