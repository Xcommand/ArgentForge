# Building a menu

A menu in the ZDoom family is a `MENUDEF` lump: blocks that name a screen, say what it's
called, and list the rows on it. You can type it out. The reason that's tedious is that almost
every row is a lookup: the widget's arguments have an order the engine reads positionally, the
cvar has a name you have to spell right, the value table is a separate block, and the text you
want to show is a `$LANGUAGE` label defined somewhere else. The **Menu Constructor** has those
lists already, because it reads them out of the engine's own widget table and out of the files
in the archive you have open.

## Opening it

Open your `MENUDEF` lump in a text tab. The toolbar above the text has a **Menu Constructor**
button, and `Tools > Menu Constructor...` does the same thing. The window floats next to the
editor and doesn't take your keyboard, so you can keep typing in the text while it's open.

It follows the text cursor. Click anywhere inside a menu block and that block becomes the one
the window shows, with the line under your cursor selected in its tree, which beats hunting for
it in a list when the file has thirty of them.

If the lump you opened holds no menu blocks, the program doesn't open an empty window on you: it
offers the `MENUDEF` lump from the same archive, or tells you this one hasn't started yet and
offers to add the first block.

## What's in the window

* Left: every block in the file, in the order the file holds them, with `New`, `Delete` and `^`
  `v` under it.
* Right, top to bottom: the block's own header (what kind it is, its `Name`, and `protected` for
  an `OptionMenu`); the tree of its lines; the arguments of the line you're on; a note that
  explains the widget and the argument you're on.
* `Preview` opens the drawn menu in its own window, described further down.

## The keys

Both lists answer the same keys, and a key does the same thing wherever it lands. What's
selected is what's affected, so a run of rows moves, copies and deletes as one.

| Key | Block list | Line tree |
| --- | --- | --- |
| `Shift` + click, or `Shift` + arrow | pick a run of menus | pick a run of lines |
| `Ctrl` + click | add one more menu to the pick | add one more line |
| `Shift` + `Up` / `Down` | move the run up or down the file | move the run inside its own block |
| `Ctrl+C` | copy the picked blocks as text | copy the picked lines as text |
| `Ctrl+V` | paste after the block on screen | paste under the line you're on |
| `Delete` | take the picked blocks out | take the picked lines out |
| `Ctrl+Z` / `Ctrl+Y` | the editor's undo and redo, without leaving the window | same |

Copy and paste go through the system clipboard as plain text, so a chunk you select in the
editor pastes into the window, and a block you copied here pastes into the editor or into
another program. Pasted lines take the indentation of the line they land next to, and a run of
blocks keeps the blank line between it that the file already used.

Everything the window writes is a normal text edit in the editor, and one operation is one undo
step: a paste of four blocks comes back with a single `Ctrl+Z`, and the window redraws itself
when it happens.

## Arguments, without typing names

The grid under the tree shows the selected line's arguments with the names the engine gives them
in its own declaration, so `label`, `cvar`, `values` are what you see rather than `argument 1`.
Next to each name is what it wants: `INT`, `FLOAT`, `STRING`, `colour`, `text colour range`, and
`, optional` when the file may leave it out.

Where an argument is a name rather than a free word, the field offers a list that floats under
it while you type and narrows with every letter. It never chooses for you: `Enter` takes a
suggestion only once you've moved onto it. Those lists come from:

* console variables: the port's own, plus every one this archive declares in `CVARINFO`, with
  the label the mod wrote next to it as the note;
* the menus defined in the file, for `Submenu` and friends;
* the value tables (`OptionValue`, `OptionString`) in the file;
* text colour range names, the engine's plus whatever this mod's `TEXTCOLO` adds;
* `$LANGUAGE` labels from the archive's language lumps;
* patches, and the widget classes this mod declares in its own ZScript.

A colour or range argument gets a colour box under the grid, and the value is painted in the
colour it names, so a wrong range is visible before it's saved. Picking from the box writes the
spelling that parameter wants: a hex value for a `Color`, a range name for `cr`.

Lines that repeat a widget on one physical line stay one line in the tree, and one undo, because
that's how the file wrote them.

## Blocks, structure and value tables

`New` asks for the kind and the name and writes the block with an empty body; the rows go in one
at a time afterwards. `protected` only means something on an `OptionMenu`, and the note beside
the name box tells you live whether the name you're typing is already taken: two blocks with the
same name append into one menu, a protected one refuses to be replaced, and a name reused across
two different kinds is what the engine stops on.

`ifgame`, `ifnotgame`, `ifoption` and `else` are lines too, so they get added from the same list,
and the tree shows what sits inside them. Moving a line that lives in a branch moves it inside
that branch and never out of it.

`OptionValue` and `OptionString` blocks are tables, so their rows are edited as three fields: the
number or word, the text, and the tooltip. Adding a row takes the number after the one above.

## The preview

`Preview` draws the block the way the game would draw it: the same scaling maths the engine uses
to place a menu on a screen, the game's own menu font, the background it would sit on, and the
text as the language table says it reads. One drawn row is one line of the file, so clicking a
row selects that line in the tree, and the arrow keys walk the game's cursor. `Enter` on a
`Submenu` takes the constructor to that block, `Backspace` comes back out.

Rows that the engine would refuse to draw are marked rather than skipped, and the header line
says what the window is working with: the resolution it imitates, the zoom, how many glyphs came
out of the font and how many language keys it read, and the background it resolved. Ask for those
numbers when something looks wrong; they usually say which half failed.

The font and its colour ranges come from the port's own pk3, which the program finds under
`ZDoom PK3 Path` in `Edit > Preferences > Editing`, at the bottom of the Base Resource Archive
page. Three settings live in `Edit > Preferences > Extra Features > Menu Constructor`: which
screen to imitate, the zoom (50 to 400 percent, where 100 puts the whole imitated screen in the
window), and whether to draw the title picture behind the menu or a flat dark.

## What it leaves alone

A word the engine's widget table doesn't know isn't an error: the parser looks a widget up as a
class built out of the word, so a mod's own `TLTPOption` is as legal as `Option`, and a line the
window has no lists for still goes back exactly as it came in. New rows are only offered in the
block kinds that hold them, and the note says so when a block type offers none.

Anything you didn't touch comes back byte for byte: the indentation, the line endings (a file can
carry both at once), the comment written behind an argument, and the quotes a word already had.

Next: [Drawing pictures](image-editor.md).
