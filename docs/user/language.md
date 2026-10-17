# Translating a language

A `$LABEL` in a menu or a script is a piece of text the game looks up in a `LANGUAGE` lump. The
lump is a list of assignments, one per string, grouped under a header that names the languages it
covers:

```
[enu default]
MENU_NEWGAME = "New Game";
VR_Menu_ModVersion = "\c7version 0.4";
```

The first thing to know is that the strings live in more than one place. A mod usually has
`LANGUAGE` for English, `LANGUAGE.ru` for Russian, sometimes `language.Menu`, `language.tips` and
`language.colors` for the strings it keeps apart by purpose. A second thing: a value carries its
own colour inside it, as `\c` plus a letter, and a line break as `\n`. Both are two characters in
the file and one meaning to the engine.

Typing that out in a text editor is work you can't see the shape of: you can't tell which language
is missing a string, and you can't tell what the text will look like in the menu. The **Language
Constructor** shows the same file as a table, one row per string and one column per language, with
the colours drawn in the colour the engine will use.

## Opening it

Open a `LANGUAGE` lump in a text tab and the toolbar above the text has a **Language Constructor**
button. `Tools > Language Constructor...` does the same thing when no file is open, or when the
file you're looking at isn't a language table. The window floats beside the editor and doesn't take
your keyboard unless you click inside it.

It follows the text cursor: click a string in the editor and its row is picked in the table. Picking
a row in the table moves the editor to that line, and if the string is written in another file of
the archive, that file opens instead, with the caret on the line.

## What's in the window

* Top left: a dropdown with every `LANGUAGE` file of the archive, a dashed line, and **Add new
  language**. The file in it is the one new strings go into, and the first column of the table.
* Next to it: **Additional**, which ticks the other files you want to see as columns. A file that
  isn't ticked isn't in the table at all, so a mod with nine language files doesn't put nine
  columns in front of you.
* The table: one row per string, the key column first, then a column per language you asked for.
  Click a header to sort by it, click it again to flip; drag a column's right border to resize it.
* The bottom row: a key, a value and a button that says where it will go, `Add to enu in
  LANGUAGE.ru`.
* Above the bottom row: a note that says why something didn't happen. An action that fails quietly
  is a bug, so a key that already exists, a file that's read-only and a string that lives in
  somebody else's lump are all written out.

The count line says what you're looking at: how many strings, how many languages, how many files
the archive holds.

## The columns are files

A column is a file you asked for, not a language the game knows. Two files that both write English
give two English columns, each with the file's name in its header, because they are two files you
have to fill in separately. One column shows the most specific tag the file declares, so a
`[enu eng en default]` header is one English column rather than four.

## Editing where it stands

One click opens the cell. `Enter` writes it, clicking another cell writes it too, `Esc` takes back
what you typed. `Ctrl+Z` and `Ctrl+Y` in the window are the text editor's undo and redo, so a
change you made here can be stepped back without leaving the table.

A long value opens as a field tall enough to read all of it: the field is as wide as the room to the
right of its cell and grows downward, and the text wraps. Where the file writes `\n`, you see those
two characters and the line breaks after them, so a line break is something you can see yourself
deleting. `Shift+Enter` adds one, `Enter` still commits. Turn the markers off under
`Settings > Extra Features > Language Constructor` if you'd rather have the plain wrapping.

A value that runs past its column is cut by the neighbour that has text, and runs on into the ones
that don't, the way a spreadsheet does.

## The grey ones are not translated

A string that a second file doesn't define is filled with the first file's text in a quieter colour.
That's a placeholder: the game will show it, because the engine falls back to the default language,
but nobody has translated it. Translate it and it stops being quiet, because it's no longer a copy
of somebody else's text. It's the table telling you where the work is without a report.

## Colour, without learning the codes

Right-click a cell:

| Item | What it does |
| --- | --- |
| `Colour...` | opens the colour picker and writes the code at the caret |
| `Remove the colour from this cell` | takes every colour code out of the value |
| `Copy` | puts the cell's raw text, codes and all, on the clipboard |
| `Paste` | writes the clipboard into the cell, at the caret if you're inside it |

The game can only draw text in the colours its own `TEXTCOLO` table knows, so the picker takes the
colour you chose and writes the nearest one of those. The table shows the words painted in that
colour with the code hidden, which is what the menu will look like; the field shows the code, which
is what the file holds.

Two things follow from that. A mod can retint a colour name in its own `TEXTCOLO` and the table
follows it, because the colours are read from the game's table rather than remembered here. And a
code you write by hand is never invented here: `\c` plus a letter is that colour's place in the
engine's list, `\c-` goes back to normal, and `\c[Name]` asks for a range by name.

## Keys you never write by hand

Type a new `$name` into a text field in the Menu Constructor, a label or a prompt or a message, and
the key is created in the file that file's dropdown points at, and as a placeholder in every file
ticked in Additional. You get the string defined the moment you use it, in all the languages you
said you keep, instead of typing a key, switching files, finding the end of the block and pasting.

## Settings

`Settings > Extra Features > Language Constructor`:

* **What 'Add new language' should do**: one `LANGUAGE` lump per language (the default, and what a
  WAD does), or a `[code]` block inside the file you're working in, for a project that keeps all of
  them in one lump.
* **Show the `\n` markers while editing a text field**: on by default, for the reason above.

The window remembers its size and where you left it. The three constructor buttons in the text
toolbar only appear over a file one of them can actually edit, so a texture doesn't offer you a
menu constructor.
