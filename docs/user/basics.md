# Basics

## What this program edits

The games this is for are the ones built on the Doom engine and the family that grew out of it:
**Doom**, **Doom 2**, **Doom 64**, **Heretic**, **Hexen**, **Strife**, **Chex Quest**, plus what the
community bolted onto them, from **Boom** and **Eternity** to **ZDoom** and everything people build on
top of it. Even **Sonic Robo Blast 2**, which is a Doom engine game whatever you thought it was.

They all carry their content the same way. A mod is one file: a `.pk3`, a `.zip`, a `.wad`. Inside it,
the parts of your mod are not files on your disk, they are *lumps* packed into that one archive. A lump
is a picture, a script, a sound, a map, a texture definition. The engine reads the archive and never
looks at your folder again.

So the unit you work in is the archive, and the unit you work on is the lump. Argent Forge is the
thing in between: it opens the archive, shows you what's inside, and gives every kind of lump an
editor that understands it. Which game and which port a lump belongs to it asks about rather than
assumes, because the same lump name in a Doom map and a Hexen map does not mean the same thing.

## First launch

You get the start page. Under **Get Started** there are four links: open an archive, open a folder as
an archive, create a new archive, create a new map. Below that, **Recent Files**, **Tip of the Day**,
and **What's Different Here**, which lists what this fork changes over SLADE3.

You can also drag a `.pk3`, `.zip` or `.wad` onto the window and drop it. That's the usual way.

## The archive window

Once something is open, the window has a tab bar along the top and one tab per archive. Inside an
archive tab:

* On the left, a thin toolbar for the chores: making folders and new lumps, moving them, bookmarking
  them, filtering the list.
* In the middle, the list of lumps. Four columns: `#` (its position in the file, which matters in a
  `.wad`), `Name`, `Size`, `Type`. Folders are just lumps that hold other lumps.
* Under the list, `Show:` and `Filter:`. Type in the filter box and the list narrows to what matches;
  the button beside it clears what you typed.

The menus are the five you'd expect: **File**, **Edit**, **View**, **Tools**, **Help**. The ones worth
knowing on day one are `File > Open` (Ctrl+O), `File > Save` (Ctrl+S), and `Edit > Preferences...`.

## Getting a lump into an editor

One click on a lump shows it in the entry area of the archive tab: quick, no new window, and you can
keep clicking down the list to look at things.

Double click (or Enter) gives the lump its own tab. That's the editor you'll actually work in, and
each tab has its own undo history.

Two things happen instead of a tab for special lumps: a texture list (`TEXTURE1`, `TEXTURE2`,
`PNAMES`) opens the texture editor, and a map opens the map window after asking which game and port
the map is for.

If a lump already has a tab and you click it in the list, the program goes to that tab rather than
opening a second editor on the same bytes. Two editors on one lump is how files get corrupted.

## The colours in the list

A lump's name changes colour to tell you what state it's in:

* **Blue**: you've edited it.
* **Green**: it's new, it wasn't in the file when you opened it.
* **Red**: it's locked, which is a thing you set on purpose to stop editing it.
* **Gold, in italics**: it holds changes and nothing is showing them.

That last one is the important one. Closing a tab in this program asks you nothing, because there's
nothing to answer: your work goes into the lump in memory and stays there, and the lump turns gold so
it can't sit there quietly forgotten. `File > Save` (Ctrl+S) is what writes the archive to disk. Until
you save, none of it is in the file, and everything is undoable.

## When something goes wrong

Ctrl+Z undoes the last change, Ctrl+Y redoes. `View > Undo History` (Ctrl+3) opens the list of what
you can step back through, which is a lot less scary than guessing.

## Things this build does that SLADE doesn't

They get their own pages later; these are the ones you'll bump into in the first hour.

* Rest the mouse on a sprite frame in a script and the picture it names opens above the line. Hold
  SHIFT and the whole state block runs at Doom speed.
* Open a DECORATE or ZScript lump and the toolbar has a **Constructor** button. It opens a floating
  window that edits the actor's flags, properties and states for you, follows the text cursor, and
  writes the code while you click. Most of DECORATE never has to be typed.
* In the picture editor, Q and W pick the brush and the eraser, ALT with the wheel sets opacity, and
  ALT with the right button dragged over the picture sets size sideways and feather up and down. The
  numbers show themselves while you do it. SHIFT with the left button draws a straight line from the
  end of the last stroke, which is the difference between drawing a weapon barrel and ruining one.
* `Tile` in that same toolbar lays the picture out in copies around itself, and painting wraps with it,
  which is how you make a texture with no seam in it.
* The news and tips on the start page come from beside the program, so they describe this build rather
  than whatever SLADE last posted.

## Settings

`Edit > Preferences...` opens **Argent Forge Settings**. The left side is a tree of pages. The ones
this fork adds live under their own branch, **Extra Features**: **Image Editor** and **Actor
Constructor**. Keys are rebound under **Keyboard Shortcuts**, colours under **Colours & Theme**.

Next: [Making an actor](actor.md).
