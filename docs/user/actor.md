# Making an actor

An actor is a monster, a weapon, a pickup, a decoration: anything you can point a map editor at. In
the ZDoom family of ports you describe one in DECORATE or ZScript, which is a text file inside your
archive that says what the thing inherits, what it can do, and what it looks like while it does it.

You can type that out. The reason nobody enjoys it is that most of the work is looking things up: the
spelling of a flag, whether a property wants a number or a quoted word, which of the hundred state
actions is the one that actually chases you. The **Actor Constructor** knows all of that, because its
lists are generated from the engine's own source rather than typed by someone who read a wiki.

## Opening it

Open your DECORATE or ZScript lump in a tab. The toolbar above the text has a **Constructor** button,
and `Tools > Actor Constructor...` does the same thing. The window floats over the editor and doesn't
grab your focus, so you can keep typing in the text while it's open.

The `Actor:` box at the top says who you are editing. It follows the text cursor: click anywhere
inside an actor in the editor and that actor is the one the window shows, which is quicker than
hunting for it in a dropdown when the file has forty of them.

## Flags

The **Flags** page is a tree of every flag the port understands, grouped by what it's for, with a note
under whatever you've selected. Type in `Filter:` and the tree narrows.

Each flag has three states, not two, because DECORATE does: `+` turns it on, `-` turns it off, and
writing neither means "whatever my parent said". `Add Flag` and `Remove Flag` do what they say.

`-` is worth being exact about, because it reads like a flip and isn't one. It clears that flag for this
actor, and it stays cleared whatever the parent had. The engine sets or unsets the bit and never toggles
it; it only looks like an inversion because the parent nearly always had the flag on, so unsetting it is
the change you can see. Which is why the third state matters: a flag you don't write at all is not off,
it's inherited.

Four of the thousand, so you know what you're looking at:

* `FLOAT` makes the monster change height on its own to reach you, over a pit and up a ledge. It wants
  `NOGRAVITY` with it, or gravity wins and nothing floats.
* `NORADIUSDMG` makes it immune to the splash from an explosion. A rocket hitting it directly still
  hurts.
* `COUNTKILL` decides whether killing this monster moves the kills number on the intermission screen.
  A friendly monster doesn't count even with the flag on. Put `-COUNTKILL` on something that respawns
  for ever and the percentage can actually reach 100%, which is the usual reason for it. It isn't a way
  of locking a level: the count only gates the exit when the game is run with `sv_killallmonsters`, and
  that one counts exactly the monsters the flags put in the tally.
* `BRIGHT` draws every frame at full brightness, whatever room it's standing in.

Some flags carry a category in front of their name, because they only mean something for one kind of
actor: a weapon file is full of `+WEAPON.AMMO_OPTIONAL` and `+WEAPON.NO_AUTO_SWITCH`. Two things to
know about those. `+WEAPON.AMMO_OPTIONAL` and `+AMMO_OPTIONAL` are not the same question to the engine,
and the constructor will never edit one because you asked about the other. And our flag list is the
plain actor vocabulary, so a category-prefixed name isn't in it: it lands in a group called **Unlisted**
at the end of the tree, where you can still tick it on and off, just with no note beside it.

## Properties

The **Properties** page lists what the actor can be told: health, speed, the radius of the thing, what
it drops when it dies. Pick one, type a value, press `Set`; `Remove` takes it back out. Double-click a
value already in the list to change it.

The value field tells you what the engine wants before you type it: `INT or (EXPR)`, `FLOAT`, `BOOL`,
`KEYWORD`, or `"STRING"` with the quotes, which is how it says "this one has to be in quotes". The
`(EXPR)` matters: an integer field is allowed to hold arithmetic and `$random(1,3)` instead of a plain
number, so the program warns you rather than blocking you.

Some properties want more than one part, and their words come out in the order the engine reads them, split
by commas: `ConversationID` shows `INT, INT ?, INT`. A `?` after one of those words means that part you can
leave out, and any hint that has one spells the marker out too, as
`(? = you can leave that one out)`.

Four properties, and what each one asks for:

| Property | Value | What it wants |
| --- | --- | --- |
| `Health` | `500` | an integer, or an expression standing in for one |
| `Radius` | `24` | a decimal, this is the collision width |
| `DeathSound` | `"BSPAIN"` | a sound name, so it goes in quotes |
| `RenderStyle` | `Translucent` | one of the engine's style words |

Names are where the awkward part comes in. A weapon property can be written two ways in the file,
plain `AmmoUse` or with its category in front, `Weapon.AmmoUse`, and to the engine those are the same
thing said twice. The list of them on the left is written plain, so the constructor has to pick which
line of yours a `Set` lands on. It goes: if your actor already has the qualified line, that's the one it
edits, and it keeps your spelling rather than rewriting it into the list's. If an actor somehow carries
both spellings, the one that matches exactly what you asked for wins. Two names with different
categories in front stay two different properties, because they are.

## States and actions

The **Actions** page is the library of things the engine can be told to do in a state, grouped by the
class that declares them, with the argument names shown under `Arguments:`. `Add to states` writes the
call into your actor with those names already in it, so you fill in what it asks for instead of
remembering the order.

A few, with what they ask for:

* `A_Look()` takes nothing at all. It's the stand-around-and-listen state every monster starts in.
* `A_Chase(melee, missile, flags)` is the walking-and-attacking one. The first two are the states to
  jump to when it gets in melee range or decides to fire, and you can leave both out.
* `A_SpawnProjectile(missiletype, spawnheight, spawnofs_xy, angle, flags, pitch, ptr)` is a fired shot.
  The class it spawns has to be first, and the rest have defaults behind it.
* `A_JumpIfTargetInLOS(label, fov, flags, dist_max, dist_close)` jumps to a state when the player is in
  the actor's line of sight, which is how a monster stops being stupid behind a pillar.

The ones the port still accepts but tells you not to use are greyed, with the note saying what to call
instead: `A_PlaySound` is the older sound call and the note points at `A_StartSound`, which does the
same job with the channel as a plain argument.

### What it writes in front of the call

A DECORATE call has to sit on a frame, so `Add to states` writes the frame as well. By default that's
`TNT1 A`, the sprite that has nothing drawn in it, so the new line runs its action without changing what
the actor looks like for a frame:

```
	TNT1 A 0 A_Recoil(1)
```

Two settings change what lands there, under `Extra Features > Actor Constructor`, in the group headed
"State action the constructor adds for you". `Frame to write in front of it` can ask for the frame above
and its picture instead, and then a call added under `ENMY A 4` comes out as `ENMY A 0 A_Recoil(1)`,
carrying the cycle on rather than blanking it. With `Count on to the next frame letter` ticked it counts
on from the letter above, so four adds in a row under an `ENMY A` line, with the tics set to 2, give

```
	ENMY B 2 A_Chase()
	ENMY C 2 A_Chase()
	ENMY D 2 A_Chase()
	ENMY E 2 A_Chase()
```

and Z wraps back round to A. `How long that frame lasts (tics)` is the number after the letter, 0 to
1000, and it's only asked about when the frame carries on from the one above, since with `TNT1 A` the
length doesn't matter.

Where it goes: after the line your cursor is on, if that's inside the actor's states block; anywhere
else and it finds the block and adds to the end of it, with the same indentation as its new neighbours.
Inside a `{ }` action block there's no frame to write, so it puts the call there with its semicolon and
nothing else. The whole line lands as one undo step, and the argument names come back selected, so the
next thing you type replaces them.

None of these forms is what the engine demands: they all mean the same thing to the game, which is why
the choice is in the settings rather than baked in.

## The thing number

`Thing Number:` is the number a map editor uses to drop your actor into a level. It goes where the
language reads it, which is not the same place twice: the end of the actor's own line in DECORATE, or
`MAPINFO` for ZScript. If the number is taken, the program says so instead of quietly writing a second
actor with it.

## A new one from scratch

`New Actor` opens a short dialog: the name, who it inherits from, whether it's DECORATE or ZScript, and
an optional `Replaces` for when your thing should stand in for a stock one. Tick `New file:` and it
writes the script file where you point and adds the `#include` for it, because that's the part people
forget and then spend an hour on.

## What it won't do

It edits the parts it understands. If a declaration has something in it the constructor can't parse, it
tells you and leaves the line alone rather than rewriting your file from what it guessed.

Everything it does write is a normal text edit: Ctrl+Z takes it back, and an actor you never touched
comes out of the round trip exactly as it went in, byte for byte, indentation and line endings
included.

## Watching the animation

Rest the mouse on a sprite name or a frame letter in a state line and the picture it names opens above
the line and sits there. Not the first frame of the block: the one you're pointing at.

It reads the text as you've typed it, not as it was saved. Retype `ENMY A 2` into `ENMY FDDFAAAASD 1`
and the next hover shows what you just wrote, so you're judging the animation while you're still editing
it, with nothing saved, nothing reloaded and nothing spawned in a game to check it against.

Hold SHIFT and it runs the whole block, from its first frame, at the speed the game would play it: 35
tics to the second, so a duration of 8 is a bit under a quarter of a second. That's how you find out
whether that `A_Light1` flash lands where you wanted it. Let go of SHIFT and it falls back to the frame
under the pointer; hold it and the window stays up while you move along the block.

Three settings live under `Extra Features > Actor Constructor`, in the group headed "Sprite preview,
when the mouse rests on a state line":

* `Scale (%)`, 100 to 500, how big the picture is drawn. It stays blocky at any size on purpose, since a
  blurred sprite tells you nothing about the pixels you're looking at.
* `Playback speed:`, from `Doom speed` down to `128x slower`. It only ever slows a block down, because
  the animation you need to look at is the one too quick to make out.
* `Play the animation round again`, which keeps replaying a block even when the script never loops it.
  For looking at the cycle, not a statement about your actor.

Two things that would otherwise read as bugs. `TNT1` draws nothing in the game, so hovering it shows the
nearest picture in the block instead of an empty box, and the same goes for a `####` that doesn't settle
on a sprite of its own. And a frame held for `-1` means "stay here for ever", so the run stops there
until you turn that repeat setting on.

The other thing that works with no window open: while you're typing a state action, its arguments pop up
over the line, so you don't have to remember which of the seven is `spawnofs_xy`.

Next: [Drawing pictures](image-editor.md).
