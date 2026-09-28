# Drawing pictures

Double-click a PNG, a Doom picture, a patch or a sprite lump in the archive list and it opens in the
image editor: the canvas, a toolbar of brush controls along the top, and the palette down the side if
the file is an indexed one. This is the part of the program that stopped being "only for mod files" a
while ago. A lot of it works the way a real painting program works, with the mouse doing more of the
talking than the menus.

## The brush row

Left to right: the size in pixels, the brush shape button, the colour, and a slider for opacity.

* The size is shown in pixels, so `7px` is a seven pixel brush.
* The colour box is the stroke colour. Left click opens the picker, right click takes one from the
  palette, which is the difference between a picture that stays in 256 colours and one that quietly
  stops being one.
* The slider is how much a stroke changes the pixels it lands on, painting or erasing, 0 to 100.

Then five toggles, and they're the ones worth learning:

* `Fixed` makes every pass lay down exactly the opacity you set. Without it a stroke builds up where you
  cross yourself, which is what a real brush does and sometimes what you don't want.
* `Grab opacity` means picking a pixel's colour also takes its opacity into the brush. This is how you
  paint back an alpha map with the strength that was already there instead of at full.
* `Alpha protect` paints only on pixels that are already partly or fully visible, so a stroke can't spill
  outside the silhouette you've drawn.
* `Colourize` gives each pixel the brush's colour at its own brightness, which turns a grey shading pass
  into a coloured one in a single stroke. It doesn't stack opacity, so `Fixed` goes grey with it.
* `Pixel perfect` leaves out the extra pixel where a stroke turns a corner, so a diagonal drawn in two
  movements looks like one. It's only live for a one-pixel brush, since that's the case it exists for.

Rest the mouse on any of them and the whole sentence about what it does comes up in the status bar at the
bottom of the window, so you don't have to remember which of the five was the one that stops the spill.

## The brush picker

The shape button opens a small popover with the rest of the brush in it: `Size` and `Feather`, the shape
(square, circle, diamond), `Dither:` with `None` and a few ordered patterns, `Blend:` with `Normal`,
`Multiply (shadows)` and `Screen (glow)`, and three jitter sliders, `Hue jitter (%)`,
`Saturation jitter (%)` and `Brightness jitter (%)`, each 0 to 100.

The blend modes are worth a sentence on their own. They work on what the stroke started from, not on
whatever is under the brush at that moment, so running the same stroke over itself twice doesn't deepen
it the second time. Jitter does the same kind of thing for colour: the brush rolls a slightly different
hue, saturation or brightness instead of taking the colour exactly as you picked it, which is how you get
rock, dirt or cloth that isn't flat. `Apply per tip` says when that roll happens. Leave it off and the
whole stroke keeps the one colour it rolled when you pressed the button; tick it and every pixel the brush
passes over gets a fresh one, so a single stroke comes out uneven on purpose. `Reset` takes you back to a
one pixel hard brush with no jitter, no dither and no blend.

Under `Preset:` are brushes someone kept, read from `res/brushpresets` next to the program and from a
`brushpresets` folder in your settings. `Save as` writes the current settings out as a `.brush` file,
which is plain `key = value` text, one line per setting, and a preset that leaves a key out doesn't
change that setting when you load it. Copy a `.brush` file into your settings folder and it shows up in
the list, so sharing a brush is sharing one small text file.

## What the mouse does

This is the part that makes it fast, and none of it is hidden in a menu. The bottom of the brush popover
spells these out in words as well, so you can read them where you're working instead of hunting for a
page.

* `SHIFT` plus left click draws a straight line from the end of your last stroke to where you clicked,
  with the line previewed on screen while you hold it down. That's how you do weapon barrels, edges of
  armour plating and any long run of pixels you'd otherwise do freehand and ruin.
* Right click or right drag picks the colour under the pointer, and with `Grab opacity` on it takes the
  alpha too.
* `ALT` plus right drag over the picture sizes the brush sideways and feathers it up and down, with the
  current numbers shown in a box while you drag. A few pixels of wobble are ignored first so a shaky
  hand doesn't reset your brush; both that and how long the numbers stay up are settings.
* The mouse wheel zooms. `ALT` plus wheel changes brush opacity instead, five percent a notch by
  default. `CTRL` plus wheel, and `CTRL`+`SHIFT` plus wheel, pan the view, as does the side wheel.
* Middle drag pans, and the arrow keys nudge the view eight pixels.
* `Q` and `W` are the brush and the eraser. They're ordinary key bindings, listed as `Brush tool` and
  `Eraser tool` under the `Image Editor` group on the `Keyboard Shortcuts` page, so remap them if your
  hands want something else.

The four tool buttons to the left of the toolbar say what the plain click does: `Drag offsets`,
`Draw pixels`, `Erase pixels` and `Translate pixels`.

One more thing that isn't in any menu: the brush circle keeps following your pointer after it leaves the
picture, and a stroke that starts on the canvas and runs off the edge carries on drawing to the edge
instead of stopping short where your hand did. A fast drag doesn't leave a dotted line behind it either,
because the pixels between the samples get joined up.

## Tile

`Tile` in the view group redraws the picture as a loop, copies of it laid out around the original, so you
can see the seam you're actually working on instead of imagining it. `Copies around the original:` picks
how many, one to nine. Painting in this view is not a preview: a stroke that runs off one edge lands on
the other side of the tile, and it lands on the original pixels, so a seamless texture is drawn rather
than patched together afterwards.

## Undo

Every stroke is one undo step, and the canvas keeps its own history, 100 strokes by default, so Ctrl+Z
takes back painting rather than the last thing saved to the archive. Repeating the same stroke twice
leaves the pixels where the first pass left them. It remembers the file format as well, so `Convert to
...` can be taken back, including the PNG alpha and tRNS steps that go with it.

## Formats

`Convert to...` changes what kind of picture the lump is, and `Optimize PNG` shrinks what's there. The
strip under the canvas says what the picture can actually hold, ending in `Palette`, `Truecolour` or
`Alpha only`, and it tells you before you paint rather than after: a palette picture takes whatever
colour is nearest the one you chose, so a stroke of a colour the game's palette hasn't got goes where you
didn't expect. `Convert to...` to PNG is the way out when you want any colour at all.

## The settings

Under `Extra Features > Image Editor` in the settings window: `Strokes to remember (undo):`, `Wheel step
for brush opacity (%):`, the `ALT + right drag over the picture: size sideways, feather up and down`
group with `Pixels of wobble to ignore first:` and `How long the numbers stay up (ms):`, `What the wheel
zooms around:` for both textures and sprites, and `Tiled view paints the picture as a loop, so a stroke
off one edge lands on the other` with `Copies around the original:` under it.

The zoom anchor is the one to set once and forget. A texture hangs off its top left corner and a sprite
off its offset point, so the two lists aren't the same. `Textures:` gives `Classic Slade`, `Image center`
and `Cursor based`; `Sprites:` gives `Offset based`, `Image center` and `Cursor based`. The offset one is
what you want if you draw things that have to line up with something else, like a muzzle flash or a weapon
barrel: zooming keeps the offset pinned where it was. `Cursor based` is what people expect from a modern
editor, the pixel under the pointer staying under the pointer as you go in.

## Two names that are not the same thing

`Colourize` on the brush row paints the brush's colour into the picture's own brightness. `Colourise` in
the colour group is a whole-image operation on every pixel at once. One is a stroke, the other is a filter,
and the menu spelled with an `s` is the one that changes the file rather than the pixels you touch.

Next: back to [the beginning](README.md).
