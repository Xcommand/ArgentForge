Changes in this fork
====================

What we added on top of upstream SLADE, feature by feature. Kept short on purpose.

Actor Constructor
-----------------

A modeless dialog for editing GZDoom actors (DECORATE and ZScript) on the text lump that is
already open, instead of typing flags and properties by hand.

* Opens from a toolbar button or the text editor's context menu; it never steals focus, so you can
  keep typing in the editor while it stays open, and it remembers where you left it.
* It belongs to the lump you're editing: looking at a graphic, or at another archive, takes it out of
  view along with the text, and it's back where you left it when you come back to that lump.
* Every actor in the lump is listed in a dropdown, so a file with several definitions is editable
  without hunting through the text. Moving the caret into another definition in the editor picks
  that one in the dropdown, so the window and the text you are looking at stay the same actor.
* `New Actor` writes an empty definition into the text from a name, a parent, and (ZScript only)
  `abstract`, plus an optional `replaces`, and opens the constructor on it. What each field means is
  spelled out under the fields rather than in a tooltip, because DECORATE and ZScript differ here.
  `Create` is the left-hand button, where a positive action goes, with `Cancel` after it.
* `New Actor File` in the archive window (toolbar, Archive menu, or the file list's right-click `New`) writes
  the definition into a file of its own instead of into the lump you happen to be looking at. It asks which
  language and where the file goes - the folder you're browsing is offered, folders that aren't there yet get
  made - and the file lands as `ZScript` or `Decorate`, so it has its syntax colours and its Type from the
  first second. A path you type keeps the extension you gave it; an empty one takes `.zs` / `.dec`.
* That file then gets its `#include` at the head of the root `ZSCRIPT` / `DECORATE` lump, or makes that lump
  if the mod has none, with `// ActorName YYYY-MM-DD HH:MM` behind the line - the one thing a mod with 300
  includes can't reconstruct later. The path is written the way the engine reads it: from the archive's root,
  no slash in front.
* The insert follows the lump's own shape rather than a rule of thumb: above the first live `#include` and
  above the comment labelling it (that comment names the group below, not your file - unless it runs to the
  top of the lump, then it's the header and stays whole), with that line's indentation; under `version` if
  there are no includes yet; below the opening comments otherwise. A commented-out `// #include` doesn't
  count, since that path is precisely not being compiled.
* File, include line and all, one Ctrl+Z. A file already sitting at that path takes the definition if it's
  still empty, and a text lump with no actors in it offers this same flow rather than stopping at "nothing
  to edit".
* Flags are a searchable, grouped tree with a short note per flag. Tri-state: unset, set, cleared
  (`+FLAG` / `-FLAG`), which is what DECORATE actually distinguishes.
* Properties are grouped the same way, with a description per property and, where GZDoom's own
  source says so, a hint of what the value should look like, written as the type itself
  (`INT`, `FLOAT`, `BOOL`, `"STRING"`, `"SOUND"`, `"COLOR"`, `"CLASS"`) in the order the engine
  reads the parts, with `?` on any part you can leave out. A hint only: nothing is refused, because
  DECORATE and ZScript also take expressions, sums and the mod's own variables that no table can
  predict. Read out of the defaults blocks in the 874 mod files on this disk, a number-shaped
  property is 19660 times out of 19661 a plain number, and the odd one out (`Mass int.max`) is exactly
  what a hard rule would have refused to write. The one shape the engine never takes is a quoted string
  in a number's place, and the line under the field says so while you type it.
* A property the file wrote with its category (`Weapon.AmmoUse`) and the same one the list calls bare
  (`AmmoUse`) are one property: editing it from either name changes the line that is already there
  instead of adding a second one, and nobody rewrites a spelling you wrote into the other one. A flag
  is not like that - `+WEAPON.AltFire` and `+AltFire` are two different questions to the engine - so
  flags stay matched exactly, one spelling to one line.
* An `Actions` page lists the functions the engine provides for states to call, grouped by the class
  that declares them - that is what decides whether a given actor can call one - with the call written
  out (parameters, their types, what they default to) and whatever is said about it. The ones the engine
  has stopped supporting are in the colour the text editor uses for comments: still selectable, still
  insertable, just not shouting, and the line under them says what to use instead. Old mods call them,
  so they aren't left out.
* Picking one and pressing `Add to states` (or double-clicking it, or Enter in the arguments field)
  writes the call into the actor: under the line the editor's caret is on, as a frame of its own
  (`TNT1 A 0 A_...`) in a states block and as a `;`-ended statement inside a `{ }` block, with the
  semicolon and the line endings that language wants. The arguments come pre-filled with the names the
  engine gives them, so it is your text from the start and nothing is invented for you. The rest of the
  file is not touched, one Ctrl+Z takes the line back out, and an actor with no states block is
  reported rather than given one. A block written whole on one line takes the frame too, empty
  (`States {}`, the stripped-down powerup) or with a state already in it (`states { Spawn: SPKW B 1
  A_FadeOut(0.1) loop }`): the line you wrote keeps its bytes and the `}` drops down to take a line of
  its own, because no frame in the 874 mod files this was checked against shares its line with another. An actor written compact, with its whole body on
  the line that opens it (`{ Health 200 States{Spawn:` over a dozen more), is found the same as any
  other now.
* The notes are coloured the way the text editor colours the same words, so `A_JumpIfInventory` reads as
  a function, `NOGRAVITY` as a constant, `"STRING"` as a string and the rest of the sentence stays prose.
  A few sentences all in one colour is too easy to lose your place in.
* The editor helps on its own too, with the dialog closed: resting the mouse on a flag or a property name
  says what it does, and typing `A_...(` brings up the call the engine declares, with the parameters you
  can leave out marked. Both read the same generated lists as the constructor's pages, so the two can't
  drift apart.
* Resting the mouse on a state line - `POSS A 3` - opens the picture above that line instead of a note
  about the words, since there the words are a picture. It reads what's in the editor at that moment, not
  what was last saved, so a half-typed frame previews too, and it names the lump it drew so an off-by-one
  sprite shows itself. A block the archive has no picture for at all says nothing: no empty box, no
  maybe. It works in either language from the moment the lump opens, and doesn't wait for the dropdown to
  say which, since a mod's script usually has neither an extension nor a type naming itself - there the
  text decides.
* Holding SHIFT runs the whole block around that line, frame by frame at the tics written in the code,
  which is how you see a death animation without starting the game. State lines are read the way the
  engine takes them, so `TNT1` and `4CPO`, a frame string inside a pair of quotes (`TRIT "##" 3`),
  `####` (the sprite the block was already showing) and `----` (that sprite *and* that frame), `#` for
  the same picture again, `^`, `[` and `]`, `RANDOM(a,b)`, and an action written over several lines
  inside `{ }` all come out as the frames they
  are. What only the engine can settle is skipped, because this is a preview of the pictures and not a
  sim: a frame held for nought tics and `TNT1` with it have nothing to show, and a `sprite =
  GetSpriteIndex("2CPO")` inside an action picks its picture while the game runs, which no reading of the
  text can know. A frame with no picture of its own - `TNT1`, or a `####` that never settled on one -
  steps over to the nearest frame in the block that does have one, on a plain hover as much as in the
  animation, since standing on it is still standing on this block; only a block with no pictures at all
  stays quiet. And a block ending in `loop` comes round again. The reading was run over the 874 mod files on this disk: it takes all
  216594 lines that look like a state line, every one of them comes back with the pictures it names, and
  none of them comes back with nothing in it.
* The picture a frame names is looked for the way the game files them: `POSSA0` first, then `POSSA1` - so
  a sprite that has no angles of its own and always faces the camera previews as the one picture it is.
  Neither of those exists in a mod that keeps two angles in a single file, where the name says both:
  `XCMBA2A8` is the 45-degree view and, mirrored, the one opposite it. There the archive is asked for
  anything that starts with the frame, and its lowest angle is taken. Case is nobody's business either
  way, in the script or in the file's name: the engine folds a lowercase frame letter to upper as it reads
  the line, and lump and file names are folded the same before anything is matched.
  A mod packed as a source tree - everything under one folder of its own, so the art sits at
  `PB_Staging/SPRITES/WEAPONS/Slot 7/FREEZER/Ice Crystals/Shards/CSD3A0.png` - names its pictures where
  the game wouldn't look for them, and SLADE's list of pictures-by-name is built from those folders. There
  the archive itself is walked for the name instead, still rotation 0 first, still skipping the brightmaps
  that are named after the picture they brighten. A frame the file genuinely doesn't have stays quiet, the
  same as before: vanilla `SGS0` isn't in a mod that only reuses it.
* Under `Scale (%)` (100 to 500 per cent, nearest neighbour so it stays the blocky sprite
  rather than a blur) there's `Play the animation round again`, which runs the block a second time
  instead of stopping at its end, and `Playback speed`. A Doom tic is 1/35 of a second and the code counts frames in tics, so `Doom speed` is exactly
  what the game itself shows; the rest hold every frame that many times longer, to pick apart something
  that goes past before the eye catches it. Frames are timed off the clock rather than counted from the
  timer's own ticks, so a block holds the length it says instead of drifting.
* There is no Apply or OK to press: the constructor writes the moment you change something, and the
  result lands in the editor as an ordinary edit, so Ctrl+Z still works.
* Round-trip is lossless: an actor you did not touch comes back byte-identical, and for an actor
  you did edit only the defaults block is rewritten. Lines the edit didn't touch keep the indentation,
  the spacing and the line ending they were written with - even in a lump that mixes CRLF and LF one
  line at a time - so ticking one flag changes one line and nothing else moves; an actor that comes
  back to a single statement goes back onto one line the way it was. States are left exactly as they
  were, apart from the one line `Add to states` puts in, which is added rather than regenerated.
* A `defaults` block that was already empty in the file stays when you tick a flag and untick it
  again. The block the constructor empties is the one that gets dropped, because it was only put there
  for the line you took back out; `Default { }` written by hand was there first and comes back with
  its braces and its spacing.
* A definition inside a `/* */` comment is somebody's disabled code, not something to edit: the
  constructor ignores it and reads the rest of the file, instead of refusing the whole lump. The same
  inside a `defaults` block, where it matters the other way round: `+MTHRUSPECIES; */` looks like a
  flag and is really the end of a comment, and taking that line out would leave the comment open over
  the rest of the file. A line with nothing outside a comment on it stays exactly where it was.
* A thing number is written where its language reads it, which is not one place. DECORATE keeps it on
  the actor's own line, at the very end of it - after the parent and after a `replaces`, where the
  engine reaches it - and the field now reads it off that spot and puts it back there, so an actor that
  already had one shows it and a number typed in lands where the game will find it; the note under the
  field quotes the actor's line so there's no guessing where it went. ZScript has nowhere in a class
  header for a number, so the same field writes its `DoomEdNums` entry into the archive's `MAPINFO` and
  reads it back from there. Which of the two it is comes from the word the line starts with, `Actor`
  or `class`, because that's what the engine goes by - not from the language box above the editor, so a
  `class` definition in a lump labelled Decorate still has its number registered in MAPINFO rather than
  written into a header that can't hold one. Each route stops at its own ceiling: 32767 for a number
  inside a DECORATE line, 65535 for a map's thing type, and a number the line can't hold is not written
  at all rather than left in a shape the engine stops parsing on.
* What language a lump is written in is usually decided by its name, which for a mod's scripts means
  nothing at all: `test.txt` and `Monsters` are both actor files. So when the editor doesn't know, the
  text gets asked - ZScript has to write `class` and DECORATE never does - and the editor's language
  dropdown then switches to match, so the colours agree with what the constructor is editing. Reading
  the text rather than trying to parse it matters: DECORATE parses fine as ZScript, since that grammar
  grew out of it, and guessing wrong would write a mod's file back in the shape its engine can't read.
  A file that doesn't settle it is left alone and says so, with the dropdown named as the way to say it.

The data behind it (flag and property names, groups, descriptions, value kinds, and the list of
actions with their signatures) is generated from GZDoom's sources by the scripts in `scripts/gen`, not typed
by hand, and shipped as the `actor_flags.cfg`, `actor_properties.cfg` and `actor_actions.cfg`
resources. The engine's own comments above each action are mostly a developer's note to themselves, so
they are labelled as coming from the source, and the `notes` block of `actor_actions.cfg` - the only
part written by hand, and the only part a regeneration leaves alone - is where what an action is for
gets said.

Graphics editor
---------------

* Brush picker: shape (square, circle, diamond) or a dither pattern in place of a shape, size from 1 to
  99 px and feather from none to 99, so the brush is chosen instead of being scrolled blind.
* Brush presets. A brush worth keeping is a file in `res/brushpresets`, and `Save as` in the popup
  writes the shape, size, softness, dither, blend and jitter out as one. They are line-based and say
  the settings by the same names the config uses, so you can open one in the text editor and send it
  to someone; a line you leave out leaves that setting alone. Choosing one in the popup applies it and
  shows a picture of the brush next to the name. Two come with the program.
* Blend modes: the brush can lay its colour down as `Multiply` (for shadows) or `Screen` (for glow)
  instead of simply over what's there.
* Over the picture ALT is the brush key: the wheel sets opacity, and the right button dragged sideways
  sets size while up and down set feather. A box with those three numbers appears while a gesture is
  going on, hung off the pixel the brush would actually land on rather than under the pointer, since
  that's the pixel it's describing; near the bottom edge it goes above instead. A drag has to commit
  to a direction before any number moves, so the wobble of the other hand isn't taken for a second
  adjustment, and the wheel without ALT is still the zoom. How much one notch of the wheel is worth is
  under the image editor's own settings.
* The brush and the eraser answer to keys (Q and W to start with) set in the keyboard shortcut
  settings, and work while the picture itself has focus.
* Fixed, Grab opacity, Alpha protect and Colourize are toolbar buttons with icons now, not four rows of
  checkboxes: the toolbar was running out of room, and a button lights up exactly like the brush and
  the eraser do and puts its whole sentence in the status bar while the mouse rests on it. Pixel
  Perfect sits with them.
* The toolbar says the brush's size in pixels and its opacity in per cent, so what a gesture just did
  is readable without opening anything.
* The brush preview follows the pointer off the edge of the picture. Zoomed in on a small sprite it is
  bigger than the sprite, and there it used to vanish - which is precisely where you're deciding
  whether to start a stroke.
* A stroke is the line between two mouse events, not a dot at each of them. Events come far between
  when the hand moves fast, so what used to come out as dots now joins up, and since the steps stay
  inside the brush, going over a pixel twice in one stroke changes it no more than going over it once.
* `Pixel perfect`, for a one pixel brush: the line is drawn the plain way, so a diagonal is one pixel
  thin instead of a staircase, and the extra pixel at a turn is left out, so a corner stays one pixel
  thick. A bigger brush covers the difference over anyway, so the button is only live on one pixel.
* A big or soft brush no longer makes a stroke stutter. Every pixel used to ask for the whole picture
  to be repainted; now a line of stamps is one change that repaints once at the end, and a stamp that
  turned out to change nothing doesn't ask at all.
* Colour jitter, under the feather slider: how far hue, saturation and brightness may wander off the
  colour you picked, in per cent of themselves. It's for shading a texture without reaching for the
  colour box every two strokes. `Apply per tip` says when the dice are thrown - off, the stroke keeps the
  one colour it rolled when the button went down, so a stroke has one mind; on, every pixel the brush
  passes over rolls again, which is what makes a surface uneven. `Reset` puts the numbers back: one hard
  pixel, nothing wandering, no pattern.
* `Alpha protect` now keeps what its name says. It left the empty pixels alone, but a stroke over a
  visible one still made it more solid than it was, which is the one thing it promises not to do. The
  colour arrives and the transparency stays where you left it - and since `Fixed` works by setting that
  transparency, under protect the opacity slider only says how much colour lands.
* A feathered brush doesn't drag out the colour that was hiding under transparency. A Doom sprite's
  transparent pixels aren't blank - they're the palette's transparency entry, which is a bright cyan -
  and blending weighted them by how much brush passed over them instead of by how visible they are. So
  the soft edge of a stroke came out cyan, and on a paletted lump the pixel's own alpha was read from the
  palette rather than the mask, which made it opaque as well. Now a see-through pixel has as much say in
  the colour that lands on it as it has alpha, which is none.
* Separate brush opacity, so painting alpha does not have to mean painting colour.
* Painting an alpha map works. The map is one byte per pixel and no colour, and the brush was writing
  that byte as the colour's alpha - which from the picker is always 255, so every stroke came out white
  whatever was chosen - while reading the pixel back it took four bytes from a one-byte image, i.e. the
  next pixels in the row, so a feathered edge jumped between values instead of fading. Now a brush paints
  the shade it looks like (the same brightness `Convert to` uses), and how much of the brush covers the
  pixel says how far the byte moves towards it - so a soft edge comes out a ramp, and a second pass in the
  same stroke lands where the first was meant to rather than twice over. `Fixed` has no partly-there
  pixel to set on a map, so there the slider is just how much of the shade lands, and the eraser still
  takes the value down to nothing.
* Undo/redo across the whole image editor, covering the operations that previously went straight to
  disk. `Convert to` is one of them now too: a format change used to wipe the history along with the
  picture's old bytes, and it takes back with it what the lump would be written as, so the strokes
  before it survive. Offsets come back too now: dragging the picture, the two offset boxes,
  `Paste offsets` and `Modify Offsets...` all changed the file with nothing to take them back. A run
  of arrow-key clicks counts as one step, not one per pixel.
* The wheel decides what it zooms around, in the image editor's settings. A texture used to grow away
  from its top left corner, which is where a 64x64 wall goes off screen and off the useful part of the
  window; now it can grow around the middle of the picture, or around wherever the pointer is, which
  is the one that lets you keep working on the corner you're looking at. A sprite hangs off its offset
  point instead, so its list starts there rather than at the corner.
* The pointer changes when the tool does. Picking the brush or the eraser while the picture was in
  `Drag Offsets` used to leave the four-arrow cursor up until the pointer left the image and came back,
  which said the click would move the picture when it was already painting it.
* `Tile` is the seamless-texture view now, not just a preview of one. The copies go all the way around
  the original instead of off to its right and below, so every edge of it is on screen at once, and how
  many copies lie around it each way is a setting in the image editor's page (one ring by default, up to
  nine). The original keeps its outline; the copies are there to show what meets it. Switching the view
  on or off puts the middle of the picture on the middle of the window, since the two views hang the
  picture off different points and it would otherwise end up off screen while you look for it.
* And in that view the picture loops under the brush: a stroke that hangs off the right edge comes back
  in on the left, off the bottom up onto the top. That's the seam itself you're painting then, not
  something next to it. Any copy on screen can start a stroke, the eyedropper reads through the loop,
  and one stroke is still one undo.

Around the editor
-----------------

* Closing an entry tab asks nothing and throws nothing away: what was in it goes to the file, and the
  file goes gold in the archive tree to say there's work in it nobody is looking at, until somebody
  opens it again. A file already open in a tab goes to that tab when it's picked in the tree, instead
  of being opened a second time next to itself.
* A file with changes that were never saved gets a tab rather than a save prompt.
* The start page says what this build changed: its news comes from the resources beside the program,
  not from whatever SLADE last posted online, and the tips come from the same place. An archive dropped
  on the start page opens there the way it does anywhere else in the window - the web view used to eat
  the drop.
* The settings this fork adds have their own branch of the preferences tree (`Extra Features`, `Image
  Editor`, `Actor Constructor`), so they aren't mixed in with SLADE's and don't have to be hunted for.
* It's called Argent Forge now: the window titles (the main window, the map editor, the script manager),
  the exit question, Help->About, the crash dialog, the program's own log header, and the executable,
  which is `ArgentForge.exe`. What still says SLADE is in two groups: things that really are SLADE (the
  wiki the Help menu opens, the `SLADE (Light)` and `SLADE (Dark)` colour sets), and text that ends up
  inside a mod's files, where a new name would be a claim about who made them. Your settings folder
  stayed `SLADE3` on purpose, so an install of this opens with your own configuration rather than a fresh one.
* The text editor's `Colour Scheme` menu has two more entries: `Argent (Light)` and `Argent (Dark)`, next
  to SLADE's own pair, which stay. Whichever scheme you pick takes the start page's theme with it, so the
  `web_dark_theme` switch under Advanced is a result rather than something to remember, and the switch
  lands on every open text lump, not just the one you clicked in.
* The Argent pair is built on the four tones the art already uses: red for hell energy, blue for the
  filtered argent the UAC handles, gold for pure argent, green for the unstable plasma. The dark one's
  backgrounds are dark shades of those four with the text close to white. The constructor's descriptions
  and the editor's calltips take their colours from the same set, so they move along with it.
* The divider between the properties and the names they can take in the constructor is draggable and
  remembers where you left it. Growing the window now widens the values, not the list of names, and the
  value column takes whatever room the list has left, so pulling the divider shows whole values instead
  of a margin of nothing.
* The start page and the archive tree are drawn in the same four tones as the colour schemes. The tree's
  status colours keep their meanings and lose SLADE's blue: a file you've edited is blue argent, a new
  one is plasma green, a locked one is hell red, and the changed-but-nobody-has-it-open one is gold.
* Gold tells the truth now. Picking a changed file that has a tab jumps to that tab and leaves the entry
  area behind, holding the file you were last looking at, and the tree read that as somebody editing it:
  the gold one went blue without growing a tab. It only counts as watched while the area is showing the
  same file the tree has selected.
* It has its own artwork now: the flame over the hex is on the start page and in the program's icon,
  which the exe carries as well. Where the mark is only 16 or 32 pixels across (the start page tab, a
  dialog's title bar, the Linux desktop icon) it's a flat version of the same thing: hex and flame, no
  inner facets, no core, because the detailed one turns to mud at that size.
* Help->About says who made what: Simon Judd wrote SLADE3, and what's on top of it is ours.
* A crash keeps its report on this machine. SLADE's dialog had a `Send and Exit` button that posted the
  stack trace, the last log lines, the last actions and the hardware to upstream's own server; a fork's
  crash arriving there would be a report Simon can't act on and never asked for, so the button is gone
  along with the request it made. What's left is `Create GitHub Issue`, which opens this repository with
  the trace already written into the report, and `Copy Stack Trace`. The same text goes to
  `slade3_crash.log` in your settings folder, as it did before.
* The version is our own line rather than upstream's, starting at `1.0.0`. Continuing SLADE's `3.x` would
  collide with releases we don't make, and it stops telling you anything about what's in this build. What
  SLADE it was cut from (`3.2.12`) is stated next to it in Help->About and in the exe's file properties.
  From here: `1.0.1` for a fix, `1.1.0` for something you can use, `2.0.0` if something you know how to do
  changes shape. A new SLADE underneath us moves the `based on` number, not ours.
* The preferences window is called `Argent Forge Settings`. The links on the start page go to this
  repository and its wiki, with SLADE's own page listed as what it is: the original.
* The one thing that still asks upstream is the version check, and it now says what it found. It stays
  off until you tick it in Preferences, and its answer used to arrive on the start page as
  `Update Available`. A newer SLADE isn't a newer Argent Forge, so the banner reads
  `Upstream released SLADE 3.3.2, that build carries none of this fork's changes`.
* There's a written guide for someone who has never opened SLADE, in `docs/user`: the archive window,
  the actor constructor, and the picture editor's brush and mouse gestures. Help->`User Guide` and the
  start page's link list both open it.
