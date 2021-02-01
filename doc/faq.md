# FAQ

## It's not working in Play In Editor (PIE) mode!

First and foremost, **make sure you've saved all your levels**. If you're playing
in the editor (PIE) and your levels aren't saved, the data UE reports to SPUD isn't
reliable and can cause problems.

Either:

1. Always use "File > Save All Levels" before playing in the editor, or
2. Enable auto-saving of all levels before PIE in "Project Settings > Plugins > SPUD"

The Output Log will report an error about this if it detects you're running
with unsaved levels.

## It's not working in Standalone mode!

I have no idea why this is. Playing in Standalone mode from the editor randomly
resets some properties sometimes and I haven't been able to figure out why. It's
only playing Standalone from the editor:

* PIE mode works fine (if you save all levels, see above). 
* Packaged games work fine too

Having discussed this with others who have experienced other weird problems
specifically with Standalone, we've concluded that Standalone mode is just "bad". 

Don't use it. Use PIE, or package the game.

## Physics doesn't play out the same way every time I load!

Correct, they won't. Unreal Engine's physics are *non-deterministic*, which means you
won't necessarily get the exact same results every time, even if objects
positions and velocities are restored correctly.

This is not a SPUD bug, it's just the way UE's physics are. You'd get the same
result if you loaded your level and applied the same forces, each time things
would play out slightly differently.