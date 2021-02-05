# Technical Details

## Save file format

The save file format is packed binary data for efficiency. It's a chunk-based format,
loosely based on [IFF](https://en.wikipedia.org/wiki/Interchange_File_Format). 
This means new chunks can be added later without breaking the format. It is
also versioned in case breaking changes need to be included later.

The full data format is laid out in `SpudData.h`.

Save game files are self-contained and can be freely copied around; they have header
information describing them so they can be enumerated by reading a minimal amount of
data off the front of the file, including description and date.

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

## Level Data Partitioning

A save game, in addition to global data, is divided into level segments, each one 
of which is standalone. When loading a save, not all level data is loaded into 
memory ready to populate actors. Instead, the level segments in the save
are split out into separate files, and placed in an "active game cache", a
folder called SpudCache in the project save area. 


When a level is loaded, the level cache file is loaded as well, so that actors 
can be populated with state. When a level is unloaded, the actors write their 
state to the level segment which is saved in this cache. This way, individual 
level state can be loaded in and out of memory as needed, meaning the active memory
footprint of the Spud system doesn't grow as you add more levels (streaming or main maps).

Saving a game to a single file re-combines all this data; any levels in memory are
written plus all the paged out level files are concatenated back into the file
(not loaded, just piped).

## Level Data Versioning

It's entirely possible that level state can have been saved at wildly different
versions of the game. A player may not have visited, say, Region 1 for ages
in their game, but there's still state there that needs to be preserved. They
might have been in Region 6 when they last saved, but the save game must still
include all the data for Regions 1-5 as well. That data remains as it was written
when the player was last there, though. We don't want to have to load
all those other maps in order to re-serialise them all into a single save with 
the current code.

You *could* explicitly require every save game to be upgraded to re-generate 
all the data for previous levels (we may provide options for this later). But, a simpler
approach is simply to allow those older levels to be old, and upgrade them when 
you next visit. 

So this is why the level data is standalone, and includes its own class list. If
the player visits Region 1 again and the class has changed, we can still read it
(albeit using the "slow path" mapping properties individually), and when the player
saves again it'll be written in the new structure.

When I say "Level" I mean both persistent maps that you travel between, and
streamed levels that load/unload on demand as you move around a persistent map.
All are self-contained in SPUD. 

