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

<img width="506" height="831" alt="image" src="https://github.com/user-attachments/assets/fbf17910-e52c-42e1-ba8a-4e5a5318a37f" /> <img width="827" height="864" alt="image" src="https://github.com/user-attachments/assets/1b23858b-b48c-4591-9f38-5e3784e49b38" /> <img width="506" height="831" alt="image" src="https://github.com/user-attachments/assets/b0896e5c-6b24-4886-98b5-0ff80476c969" /> 


  
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

https://github.com/user-attachments/assets/1d0dad59-58c8-456d-9585-999272cd2705
* Rest the mouse on a state line and the picture it names opens above it; hold SHIFT and the
  whole block runs at Doom speed. It reads the text the way the engine takes it: `TNT1`, `####`,
  `----`, quoted frames, `RANDOM(a,b)`. Checked against every state line in the mod files on this
  disk, 216594 of them, none of them missed.
* A calltip shows a state action's arguments while you type; a flag or property name explains itself
  on hover. Both read the same lists as the constructor, so the two can't drift apart.
* A script lump with no extension and no type is worked out from its text, and the language dropdown
  switches to match.

**Menus, drawn as the game draws them**

The **Menu Constructor**: a floating window over the MENUDEF text you already have open. Its blocks
are on the left, that block's lines in the middle, the arguments of the selected line under them. It
writes your text rather than a copy of it, so the editor's Ctrl+Z is the undo for the window as well,
and a line you didn't touch comes back byte for byte the way the mod wrote it.

<img width="713" height="447" alt="The Menu Constructor with its preview open on a mod's MENUDEF" src="https://github.com/Xcommand/ArgentForge/releases/download/v1.1.0/menu-constructor.png" />

* `Preview` draws the menu the way the engine does: the same line spacing, labels right-aligned to the
  gutter the engine works out for itself, the game's colour codes, and LANGUAGE labels resolved through
  the same two passes the game makes, so a `$$SOMELABEL` shows the words a player reads rather than
  the tag. It imitates a screen you pick, because the engine takes the text size from the resolution,
  and a knob dims the title picture behind the menu when that picture fights the text. A line the
  engine would stop loading on isn't drawn at all, and the window says why.
* The lists read your archive instead of a remembered reference: the cvars it declares, its TEXTCOLO
  ranges, its LANGUAGE labels, the pictures inside it, and the widget classes it writes in its own
  ZScript, whose `Init()` becomes that line's named fields.
* SHIFT is the key to reach for: Shift+Up and Shift+Down move a block or a line, Shift+click picks a
  run of them, Shift plus the wheel over the preview sets how large it draws, Shift+Ctrl+Z redoes.
  Ctrl+C and Ctrl+V carry a block or a line between the window and the text editor in both directions,
  and Delete works in whichever of the two lists has the keyboard.
* An empty MENUDEF is a starting point rather than a dead end. The window asks where your first menu
  should hang, under the game's Options list or on the main menu, and writes both blocks it takes to
  be seen in game: the page, and the line that points at it.

<img width="525" height="397" alt="Where the first menu of a mod hangs" src="https://github.com/Xcommand/ArgentForge/releases/download/v1.1.0/start-a-menu.png" /> <img width="476" height="576" alt="The archive's own cvar names offered under a field" src="https://github.com/Xcommand/ArgentForge/releases/download/v1.1.0/name-offers.png" />

**Image editor**


https://github.com/user-attachments/assets/f490a357-4ce6-4358-9244-cde8b9b4d829


* A brush picker: shape or dither, size up to 99 px, feather, blend mode. And brush presets that are
  plain files, so a brush worth keeping can be sent to someone.
* Over the picture, ALT does the settings: the wheel for opacity, the right button dragged sideways
  for size and up and down for feather, with the numbers shown while the gesture runs. Q and W pick
  the brush and the eraser, on keys you can rebind.

<img width="424" height="349" alt="image" src="https://github.com/user-attachments/assets/d44c50bd-3ca9-4da0-8c92-35d5daca3119" />
  
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
* The settings this fork adds live in their own branch of the preferences tree, and in their own
  folder on disk, so an install of this and an install of SLADE stop writing over each other. A first
  run offers to pull an old SLADE configuration across, and the same import sits under
  `Settings > Extra Features`.
* `Open Containing Folder` shows where a selection lives, from the entry list's right-click menu, its
  left toolbar or the `Entry` menu. It is there for a project opened as a folder, which is how most
  mod source trees sit on disk these days.
* AceZDL can open a mod with this editor: `Settings > Extra Features` writes the path into the
  launcher's `Config.xml` and keeps the one that was there, so it can be put back.

### Supporting this fork

There is no donation link here. This was built because I use it every day and it saves me time, and
that is the whole payment. What genuinely helps instead:

* **Say when something is wrong.** A modder running into a bug is the only kind of tester a fork
  like this has.
* **Say what you keep working around.** Every tool above exists because someone got tired of a
  specific chore, and I can only see my own.
* **Show what you made with it.** I'd like to know the thing got used.

Didn't like it? Well original SLADE is one click away, still published, still untouched, and nobody is making you use
this. What I'd rather not spend on is arguing about it.

It's GPLv2, same as SLADE. Feel free to fork the project under a name you like better and build it
the way you'd have done it. I mean that, and I would happily take the ideas back.

### Credits

* [SLADE3](https://github.com/sirjuddington/SLADE) and everything it is made of, without which there
  is nothing here to fork.
* The GZDoom sources, which the flag, property and action lists are generated from rather than
  typed out or copied from a wiki.
* The Doom engine modding community, for the 874 mod files this thing gets checked against.
