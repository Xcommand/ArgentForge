# Reading a mod's code without hunting

A mod is a hundred small files, and the interesting part of any one of them is written somewhere
else: the actor a state jumps to, the sound a call plays, the sprite a frame line names, the cvar
an `if` reads. Right-click the name and the menu offers where it is defined, in this archive, with
the file named in brackets:

```
Go to VR_Rocket (ZScript, Projectiles.zs)
Go to soldier.attack (SNDINFO sound, SNDINFO.guns)
Go to sv_aircontrol (CVARINFO cvar, CVARINFO)
```

Pick one and the file opens there. If the definition is in the file you are already reading, the
view just moves to the line and the name is selected.

## What it answers

* Classes and actors, in ZScript and DECORATE, including the parent in `class Foo : Bar` and what
  `replaces X` points at.
* State labels, the bare word in a frame line or a `goto`. A label the actor only inherits is
  found in the class that writes it.
* Functions the mod writes, `function` and DECORATE's `action`, from wherever they are called. A
  routine the parent writes is found in the file that writes the parent.
* Sounds: the alias a `SNDINFO` line writes, the file that line quotes, and the audio file itself.
* Cvars from `CVARINFO`, levels from `MAPINFO` (both `map MAP01` and its `lookup` name), and
  `LANGUAGE` keys, whether the file writes them as `NAME = "text";` or as a spreadsheet.
* Sprites, flats, textures and patches by the file they are drawn from, and a map by its own
  folder. A map opened this way answers the back key only after you click a text again.

Where a name has more than one definition, the menu lists them in the order the engine reads them,
so the first answer is the one the game uses. Classes are read first-write-wins; sounds, cvars,
levels and translations are overwritten by a later file, and that one is offered first.

## Going back

A jump remembers where you were. `Alt+Left` and `Alt+Right` walk that trail, the mouse's side
buttons do the same, and the right-click menu lists the jump at its top: `Back to Sniper.zs`. It
works after a jump that landed on a picture, not just between texts, and a place whose
file has since been deleted is skipped rather than blocking the button.

Both keys are ordinary keybinds, listed under **Text Editor** in the input settings, so they can be
moved to other keys.

## When it says nothing

Over a file with names in it the menu always answers, even when the answer is none: a grey line
says how many classes, names and files were read. That is the difference between "this mod has no
such thing" and "the archive was never looked at", and it is the first thing to check when a jump
you expected is missing.
