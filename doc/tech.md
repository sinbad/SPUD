# Technical Details

## Save file format

The save file format is packed binary data for efficiency. It's a chunk-based format,
loosely based on [IFF](https://en.wikipedia.org/wiki/Interchange_File_Format). 
This means new chunks can be added later without breaking the format. It is
also versioned in case breaking changes need to be included later.

The full data format is laid out in `SpudData.h`.

## Property Data

Property data is packed tightly for efficiency since it comprises
the majority of the data. However, it's still able to tolerate changes over time.
For each class encountered in a level, a description of the class is stored in 
the level data. On restore, if the runtime class is the same structure as stored
on disk, the restore uses a "fast path" to re-populate the instances in the level,
where it doesn't need to perform any property lookups because the structure is
identical. 

If, however, the class has changed since the save was taken, that class metadata is
used to patch what properties still match into the runtime instance. This
the "slow path" allows you to restore old saves, just a little slower. The next
save will have the new class structure and will restore faster next time.

## Level Data Granularity 

The save game is divided into level segments, each one is relatively standalone.
For example class data is kept per level, even if the same classes are used in
lots of levels. 

This is necessary because a player may not have visited, say, Region 1 for ages
in their game, but there's still state there that needs to be preserved. They
might have been in Region 6 when they last saved, but the save game must still
include all the data for Regions 1-5 as well. We don't want to have to load
all those other maps in order to serialise them all into a single save.

So instead, we operate on a level at a time, updating the data just for the levels
we load as part of play, and re-using the other level data segments from when they
were last visited. Because of this, it's entirely possible if you've done 
patches to your game, that you added saved properties to a class that is used in many 
regions. You *could* explicitly require every save game to be upgraded to include
this data in every level (we may provide options for this later). But, a simpler
approach is simply to allow those older levels to be old, and upgrade them when 
you next visit. 

So this is why the level data is standalone, and includes its own class list. If
the player visits Region 1 again and the class has changed, we can still read it
(albeit using the "slow path" mapping properties individually), and when the player
saves again it'll be written in the new structure.

When I say "Level" I mean both persistent maps that you travel between, and
streamed levels that load/unload on demand as you move around a persistent map.
All are self-contained in SPUD. 

