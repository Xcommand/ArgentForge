# Reporting something

This is a fork, so the first thing to work out is whose bug it is. If the same thing happens in
[SLADE itself](https://github.com/sirjuddington/SLADE), it belongs over there - upstream won't see it
if you file it here, and I can't fix what I haven't inherited. If it only happens in Argent Forge, or
it's about something this fork added (the actor constructor, the sprite preview on a state line, the
painting tools, the start page), then it's mine and I want to hear about it.

## Bugs

Say which version you're on - it's in Help -> About - your OS, and what you did right before it went
wrong. A mod file that reproduces it is worth more than a paragraph of description, so attach one if
you can share it.

If it crashed, the dialog that came up has the stack trace in it. Press `Copy Stack Trace` and paste
that into the report.

## Ideas for changes

Check the [open issues](https://github.com/Xcommand/ArgentForge/issues) first so we aren't having the
same conversation twice, then file one. What you'd want it for matters more than what you'd press: a
report that says "I have to do X every time I add a weapon" is easier to work with than a spec, and
sometimes there's a better answer than the one suggested.

## Pull requests

The gates this project is checked against are in `scripts/gates/`, and `COMPILE.md` says how to build.
If a change touches how DECORATE or ZScript is read or written, run the corpus sweep before asking -
every file it's given has to come back byte-identical when nothing was edited.

I'll take fixes for things this fork does. I'm not going to merge a reimplementation of something
upstream is already working on, and I'd rather not take wholesale changes that would make a future
merge from SLADE painful.
