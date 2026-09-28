<p align="center">
  <img src="dist/res/logo_icon.png" width="128" height="128" alt="Argent Forge logo">
</p>

## Argent Forge

It's more than just a Doom editor.

### About

Argent Forge is a fork of [SLADE3](https://github.com/sirjuddington/SLADE), the editor for Doom
engine resources. "Doom engine" there means the whole family: Doom, Heretic, Hexen, Strife, Chex
Quest, and the ports that run them, from Boom and Eternity to ZDoom and what people build on it.
Everything SLADE3 does, this still does: archives, lumps, patches, textures, conversions, all of it.
What changed is how long a task takes in it.

I sat down and did this seriously on the 9th of September 2026. Before that it was a wish I kept
describing to myself. The reason is a small one: I was spending more time on DECORATE than on the
monster. Looking a flag name up, typing it, finding the typo, checking what the property actually
wanted. None of that is modding, and the engine knows all of it already. So the Actor Constructor
came first. Then the sprite under the mouse, because a state line is a picture. Then the brush,
because I kept zooming in to see what I was painting.

Less typing, more modding. That's the whole idea, and everything below is in service of it.

At some point keeping that to myself stopped making sense. An editor that saves you an hour a week
is not a personal tool any more, it's an hour other modders are also losing.

The build is one zip, under [Releases](https://github.com/Xcommand/ArgentForge/releases/latest):
unzip it wherever you like and run `ArgentForge.exe`. Nothing installs and no file is written beside
the exe. It's a 64-bit Windows build.

If you know SLADE, you can use this without reading anything first. Every difference is listed in
[CHANGES.md](CHANGES.md) and on the program's own start page.

If you don't know SLADE, start at [docs/user](docs/user/README.md). It's written for the person who
has never opened the thing and doesn't want to ask anyone where the save button is.

### Features

Full SLADE3 functionality (archive and resource editing, tabs, the supported game formats, the text
editor, graphic conversion, the texture editor) with the usability of all of it worked over, and
this on top:

**Actors, without typing them**
* The **Actor Constructor**: a floating window that edits an actor's flags, properties and states
  and never steals your focus. It follows the text caret, so the actor you are looking at is the one
  you are editing.
* Flags and properties as a searchable, grouped tree with a note on each one, and tri-state
  `+FLAG` / `-FLAG` because DECORATE really does tell those apart.
* A value field says what the engine wants there: `INT`, `FLOAT`, `"STRING"`, `"SOUND"`.
* The whole library of state actions, grouped by the class that declares them, with `Add to states`
  writing the call into your actor with the engine's own argument names in it.
* `New Actor` and `New Actor File` create the definition, the file and the `#include` for it, in the
  place the include belongs, all of it one Ctrl+Z.
* A DoomEd number goes where its language reads it: the end of the DECORATE line, or `MAPINFO` for
  ZScript.
* Round-trip is lossless. An actor you didn't touch comes back byte-identical; ticking one flag
  changes one line and leaves its indentation and line ending alone.

**Code editor**
* Rest the mouse on a state line and the picture it names opens above it; hold SHIFT and the
  whole block runs at Doom speed. It reads the text the way the engine takes it: `TNT1`, `####`,
  `----`, quoted frames, `RANDOM(a,b)`. Checked against every state line in the mod files on this
  disk, 216594 of them, none of them missed.
* A calltip shows a state action's arguments while you type; a flag or property name explains itself
  on hover. Both read the same lists as the constructor, so the two can't drift apart.
* A script lump with no extension and no type is worked out from its text, and the language dropdown
  switches to match.

**Image editor**
* A brush picker: shape or dither, size up to 99 px, feather, blend mode. And brush presets that are
  plain files, so a brush worth keeping can be sent to someone.
* Over the picture, ALT does the settings: the wheel for opacity, the right button dragged sideways
  for size and up and down for feather, with the numbers shown while the gesture runs. Q and W pick
  the brush and the eraser, on keys you can rebind.
* SHIFT-click paints a straight line from where the last stroke ended; `Pixel perfect` draws a
  one-pixel diagonal as one pixel, corners included.
* A stroke is now the line between two mouse events instead of a dot at each of them, so a fast hand
  gets a line and not a dotted path.
* Colour jitter for shading without reaching for the colour box; `Alpha protect` and feathered edges
  fixed so they stop changing the transparency you asked them not to; painting an alpha map actually
  works.
* Undo and redo across the whole editor, including `Convert to` and offsets, which used to go
  straight to disk.

**Around the editor**
* Closing a tab asks nothing and throws nothing away; a file holding unsaved work goes gold and
  italic in the tree, so nothing stays pending quietly.
* The start page speaks for this build. Its news and tips come from beside the program, not from
  whatever SLADE last posted online.
* The settings this fork adds live in their own branch of the preferences tree.

### Supporting this fork

There is no donation link here. This was built because I use it every day and it saves me time, and
that is the whole payment. What genuinely helps instead:

* **Say when something is wrong.** A modder running into a bug is the only kind of tester a fork
  like this has.
* **Say what you keep working around.** Every tool above exists because someone got tired of a
  specific chore, and I can only see my own.
* **Show what you made with it.** I'd like to know the thing got used.

Most of the code in this fork was typed with an AI assistant, Qwen3.8-Flash, at the moment this was
written. Every edit went under my direction, and nothing shipped that I hadn't read and checked line
by line; the corpus of real mod files this is tested against is mine, and so is every decision about
what the tool should do. If the AI part rules this out for you, skip it without hard feelings. The
original SLADE is one click away, still published, still untouched, and nobody is making you use
this. What I'd rather not spend on is arguing about it.

It's GPLv2, same as SLADE. Feel free to fork the project under a name you like better and build it
the way you'd have done it. I mean that, and I would happily take the ideas back.

### Credits

* [SLADE3](https://github.com/sirjuddington/SLADE) and everything it is made of, without which there
  is nothing here to fork.
* The GZDoom sources, which the flag, property and action lists are generated from rather than
  typed out or copied from a wiki.
* The Doom engine modding community, for the 874 mod files this thing gets checked against.
