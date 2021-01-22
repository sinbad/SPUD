#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpudData, Verbose, Verbose);

// Chunk IDs
#define SPUDDATA_SAVEGAME_MAGIC "SAVE"
#define SPUDDATA_SAVEINFO_MAGIC "INFO"
#define SPUDDATA_METADATA_MAGIC "META"
#define SPUDDATA_CLASSDEFINITIONLIST_MAGIC "CLST"
#define SPUDDATA_CLASSDEF_MAGIC "CDEF"
#define SPUDDATA_CLASSNAMEINDEX_MAGIC "CNIX"
#define SPUDDATA_PROPERTYNAMEINDEX_MAGIC "PNIX"
#define SPUDDATA_NAMEDOBJECT_MAGIC "NOBJ"
#define SPUDDATA_SPAWNEDACTOR_MAGIC "SPWN"
#define SPUDDATA_DESTROYEDACTOR_MAGIC "KILL"
#define SPUDDATA_LEVELDATAMAP_MAGIC "LVLS"
#define SPUDDATA_LEVELDATA_MAGIC "LEVL"
#define SPUDDATA_GLOBALDATA_MAGIC "GLOB"
#define SPUDDATA_GLOBALOBJECTLIST_MAGIC "GOBS"
#define SPUDDATA_LEVELACTORLIST_MAGIC "LATS"
#define SPUDDATA_SPAWNEDACTORLIST_MAGIC "SATS"
#define SPUDDATA_DESTROYEDACTORLIST_MAGIC "DATS"
#define SPUDDATA_PROPERTYDEF_MAGIC "PDEF"
#define SPUDDATA_PROPERTYDATA_MAGIC "PROP"
#define SPUDDATA_CUSTOMDATA_MAGIC "CUST"
#define SPUDDATA_COREACTORDATA_MAGIC "CORA"

#define SPUDDATA_INDEX_NONE 0xFFFFFFFF
#define SPUDDATA_PROPERTYID_NONE 0xFFFFFFFF
#define SPUDDATA_PREFIXID_NONE 0xFFFFFFFF

// None of the structs in this file are exposed to Blueprints, they're all internal.
// Blueprints and external code should just use the API on USpudSubsystem

// Serialized data format is binary chunk-based, inspired by IFF (like lots of things!)
// For efficiency not everything is a separate chunk, the idea is that if anything breaking changes within a chunk
// then the version number of the format will be incremented, and fallback load paths provided for older versions.
// You can also deprecate chunks and replace them with newer versions with different "Magic" codes.
// Parsing the format automatically knows how to skip over chunks it doesn't need / understand.

// The entire save file is a chunk of its own, just in case we ever need to embed this in something larger than a file:
// Header:
// - MAGIC (char[4]) "SAVE"
// - Total Data Length (uint32) (validation check, excluding header, including all nested chunks)
// Data:
// - Save Info Chunk
// - Global Data Chunk
// - Level Chunks x N

// Save Info is a chunk of the minimal data needed to describe the save game, for easy access to a description of the
// save. Global Data includes what map the player is on, and the state of global objects like GameInstance.
// Each Level chunk contains all the state for objects owned by a single level - persistent or streamed.
// Full details are in the structs listed in the rest of this file.

#define SPUDDATA_GUID_KEY_FORMAT EGuidFormats::DigitsWithHyphens

enum ESpudStorageType // (stored as uint16 but not using enum class to make bitwise ops easier)
{
	// All of these are serilized as per their underlying types
	ESST_UInt8 = 0,
    ESST_UInt16 = 1,
    ESST_UInt32 = 2,
    ESST_UInt64 = 3,
    ESST_Int8 = 4,
    ESST_Int16 = 5,
    ESST_Int32 = 6,
    ESST_Int64 = 7,
    ESST_Float = 8,
    ESST_Double = 9,
	
    ESST_Vector = 20,
    ESST_Rotator = 21,
    ESST_Transform = 22,
	ESST_Guid = 23,
    	
	ESST_CustomStruct = 29,

	ESST_String = 30,
	ESST_Name = 31,

	/// Unknown is a placeholder fallback
	ESST_Unknown = 0x0F00,

	// - Values 0x1000 upwards are flags
	/// ArrayOf type is bitwise combined with other types to indicate multiple elements
	/// 1. Element Count (uint16 - max 65536 elements)
	/// 2. Data x Element Count
	ESST_ArrayOf = 0x1000,
	ESST_Single = 0x0 // to indicate not an array, useful sometimes
	
};

/// Common header for all data types
struct FSpudChunkHeader
{

	uint32 Magic; // Identifier
	uint32 Length; // Excluding header, including nested data

	static constexpr int64 GetDataSize() { return sizeof(uint32) + sizeof(uint32); }

	char MagicFriendly[4]; // not saved, for easier debugging

	FSpudChunkHeader(): Magic(0), Length(0), MagicFriendly{' ', ' ', ' ', ' '}
	{
	}

	static uint32 EncodeMagic(const char* InMagic)
	{
		check(strlen(InMagic) == 4)
        return InMagic[0] +
            (InMagic[1] << 8) +
            (InMagic[2] << 16) +
            (InMagic[3] << 24);		
	}
	static void DecodeMagic(uint32 InMagic, char* OutMagic)
	{		
        OutMagic[0] = InMagic & 0xFF;
		OutMagic[1] = (InMagic >> 8) & 0xFF;
		OutMagic[2] = (InMagic >> 16) & 0xFF;
		OutMagic[3] = (InMagic >> 24) & 0xFF;
	}
	static FString MagicToString(const char* InMagic)
	{
		// No null-terminator
		return FString(4, InMagic);
	}

	void Set(const char* InMagic, uint32 InLen)
	{
		Magic = EncodeMagic(InMagic);
		Length = InLen;
		DecodeMagic(Magic, MagicFriendly);
	}

	bool IsMagicEqual(const char* InMagic) const
	{
		return EncodeMagic(InMagic) == Magic;
	}

	friend FArchive& operator<<(FArchive& Ar, FSpudChunkHeader& Data)
	{
		Ar << Data.Magic;
		Ar << Data.Length;

		if (Ar.IsLoading())
			DecodeMagic(Data.Magic, Data.MagicFriendly);
		
		return Ar;
	}
};

struct FSpudChunkedDataArchive : public FArchiveProxy
{
	FSpudChunkedDataArchive(FArchive& InInnerArchive)
        : FArchiveProxy(InInnerArchive)
	{
	}

	/// Try to read the header of the next chunk and populate OutHeader
	/// Optionally returns the archive position to the previous position afterwards so doesn't
	/// change the position in the stream. Returns whether a valid header was found
	bool PreviewNextChunk(FSpudChunkHeader& OutHeader, bool SeekBackToHeader = true);
	bool NextChunkIs(uint32 EncodedMagic);
	bool NextChunkIs(const char* Magic);
	void SkipNextChunk();
};


struct FSpudChunk
{

	FSpudChunkHeader ChunkHeader;
	int64 ChunkHeaderStart;
	int64 ChunkDataStart;
	/// The end of this data; only valid when reading.
	int64 ChunkDataEnd;
	
	virtual ~FSpudChunk() = default;
	virtual const char* GetMagic() const = 0;
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) = 0;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) = 0;

	bool ChunkStart(FArchive& Ar);
	void ChunkEnd(FArchive& Ar);
	bool IsStillInChunk(FArchive& Ar) const;
};

/// Definition of a property on a class
/// We store a list of these for each known leaf class
/// Each instance will store related offsets within its data buffer (those can be variable because
/// of variable-size properties, like strings and arrays)
struct FSpudPropertyDef
{
	/// ID referencing the name of the property in the name index. Only the last part of the name
	uint32 PropertyID;
	/// ID referencing the prefix (containing struct property name), if any
	uint32 PrefixID;
	/// The storage type, includes collection flags like ArrayOf
	uint16 DataType;
	
	FSpudPropertyDef()
        : PropertyID(SPUDDATA_PROPERTYID_NONE), PrefixID(SPUDDATA_PREFIXID_NONE), DataType(ESST_Unknown){}
	FSpudPropertyDef(uint32 InPropNameID, uint32 InPrefixID, uint16 InDataType)
		: PropertyID(InPropNameID), PrefixID(InPrefixID), DataType(InDataType) {}
};


/// Definition of a class, to share property definitions
struct FSpudClassDef : public FSpudChunk
{
	FString ClassName;
	
	/// Map from PrefixID -> Map of PropertyNameID to property definition index
	TMap<uint32, // Prefix ID
		TMap<uint32, int>> PropertyLookup; // Property Name ID -> Index
	/// Actual property storage, these indexes are what actual instances store offsets against
	TArray<FSpudPropertyDef> Properties;

	virtual const char* GetMagic() const override{ return SPUDDATA_CLASSDEF_MAGIC; }
	
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) override;

	/// Add a new property and return its index
	int AddProperty(uint32 InPropNameID, uint32 InPrefixID, uint16 InDataType);
	/// Find a property or null. IDs are from PropertyNameIndex
	const FSpudPropertyDef* FindProperty(uint32 PropNameID, uint32 PrefixID);
	/// Find a property index or -1. IDs are from PropertyNameIndex
	int FindPropertyIndex(uint32 PropNameID, uint32 PrefixID);
	/// Find a property index or add it if missing. IDs are from PropertyNameIndex
	int FindOrAddPropertyIndex(uint32 PropNameID, uint32 PrefixID, uint16 DataType);

	/// Whether this Class definition matches the current runtime class properties exactly
	/// I.e. iterating properties on current objects leads to the same sequence as Properties array in this class
	/// If Matching, this means we don't have to look up every property on restore, we can just iterate both sides
	enum ClassDefMatch
	{
		NotChecked,
        Matching,
        Different
    };
	mutable ClassDefMatch RuntimeMatchState = NotChecked;
	
	/// Return Whether this Class definition matches the current runtime class properties exactly
	/// Only calculated once after loading
	bool MatchesRuntimeClass(const struct FSpudClassMetadata& ParentMeta) const;
	
};

/// A chunk which just holds an array of bytes
/// This is mostly for property data and core/custom data (which can be anything)
struct FSpudDataHolder : public FSpudChunk
{
	TArray<uint8> Data;

	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) override;

	virtual void Reset();

};
/// Holder for property data that we automatically populate
struct FSpudPropertyData : public FSpudDataHolder
{
	/// List of byte offsets into the data buffer where each property can be found
	/// Properties are ordered as per the relevant FSpudClassDef
	/// We need offsets per-instance not per-class because data offsets can be different per instance
	// (string lengths, array lengths can vary)
	TArray<uint32> PropertyOffsets;
	
	virtual const char* GetMagic() const override { return SPUDDATA_PROPERTYDATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) override;

	virtual void Reset() override;

};
/// Holder for actor core data; not properties e.g. transform
struct FSpudCoreActorData : public FSpudDataHolder
{
	virtual const char* GetMagic() const override { return SPUDDATA_COREACTORDATA_MAGIC; }
};
/// Holder for any custom data you want to store on top of SaveGame properties
struct FSpudCustomData : public FSpudDataHolder
{
	virtual const char* GetMagic() const override { return SPUDDATA_CUSTOMDATA_MAGIC; }
};

// Abstract general def of object data
struct FSpudObjectData : public FSpudChunk
{
	// Properties derived from core data like transform rather than UE properties. Chunk copied from stream
	FSpudCoreActorData CoreData;
	// Content of actual UE properties. Chunk of property data copied from the stream
	FSpudPropertyData Properties;
	// Chunk of custom data (may be empty, only present if ISpudCallback implementation populates it)
	FSpudCustomData CustomData;
};


/// Objects which can be identified just by a name
/// Includes actors in a level, global objects
struct FSpudNamedObjectData : public FSpudObjectData
{
	FString Name;

	/// Key value for indexing this item; name is unique in the level
	FString Key() const { return Name; }

	virtual const char* GetMagic() const override { return SPUDDATA_NAMEDOBJECT_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) override;
};

/// An actor which was spawned at runtime, not present in a level
struct FSpudSpawnedActorData : public FSpudObjectData
{
	uint32 ClassID; // ID for the ClassName (see FSpudClassNameIndex) 
	FGuid Guid;

	/// Key value for indexing this item; name is unique in the level
	FString Key() const { return Guid.ToString(SPUDDATA_GUID_KEY_FORMAT); }

	virtual const char* GetMagic() const override { return SPUDDATA_SPAWNEDACTOR_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) override;
};


struct FSpudDestroyedLevelActor : public FSpudChunk
{
	FString Name;

	/// Key value for indexing this item; name is unique in the level
	FString Key() const { return Name; }

	FSpudDestroyedLevelActor() {}
	FSpudDestroyedLevelActor(const FString& InName) : Name(InName) {}

	virtual const char* GetMagic() const override { return SPUDDATA_DESTROYEDACTOR_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) override;
};

/// A map of nested structs, which need to be written out in a more complex way and have their own Key() method
template <typename K, typename V>
struct FSpudStructMapData : public FSpudChunk
{
	TMap<K, V> Contents;

	void Empty() { Contents.Empty(); }

	virtual const char* GetChildMagic() const = 0;

	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override
	{
		if (ChunkStart(Ar))
		{
			// Just write values, those will write chunks
			for (auto && Tuple : Contents)
			{
				Tuple.Value.WriteToArchive(Ar);
			}
			ChunkEnd(Ar);
		}
			
	}

	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 Version) override
	{
		if (ChunkStart(Ar))
		{
			Contents.Empty();

			// Detect chunks & only load compatible
			const uint32 ChildMagicID = FSpudChunkHeader::EncodeMagic(GetChildMagic());
			V ChildData;
			while (IsStillInChunk(Ar))
			{
				if (Ar.NextChunkIs(ChildMagicID))
				{
					ChildData.ReadFromArchive(Ar, Version);
					Contents.Add(ChildData.Key(), ChildData);						
				}
				else
				{
					Ar.SkipNextChunk();
				}
			}
			ChunkEnd(Ar);
		}		
	}

	void Reset()
	{
		Contents.Empty();
	}
	
};

template <typename T>
struct FSpudArray : public FSpudChunk
{
	TArray<T> Values;

	virtual const char* GetChildMagic() const = 0;

	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override
	{
		if (ChunkStart(Ar))
		{
			// Just write values, those will write chunks
			for (auto && Item : Values)
			{
				Item.WriteToArchive(Ar);
			}
			ChunkEnd(Ar);
		}
	}

	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 Version) override
	{
		if (ChunkStart(Ar))
		{
			Values.Empty();

			// Detect chunks & only load compatible
			const uint32 ChildMagicID = FSpudChunkHeader::EncodeMagic(GetChildMagic());
			T ChildData;
			while (IsStillInChunk(Ar))
			{
				if (Ar.NextChunkIs(ChildMagicID))
				{
					ChildData.ReadFromArchive(Ar, Version);
					Values.Add(ChildData);						
				}
				else
				{
					Ar.SkipNextChunk();
				}
			}
			ChunkEnd(Ar);
		}		
	}

	void Reset()
	{
		Values.Empty();
	}
	
};

struct FSpudGlobalObjectMap : public FSpudStructMapData<FString /* FName or Guid String */, FSpudNamedObjectData>
{
	virtual const char* GetMagic() const override { return SPUDDATA_GLOBALOBJECTLIST_MAGIC; }
	virtual const char* GetChildMagic() const override { return SPUDDATA_NAMEDOBJECT_MAGIC; }
};
struct FSpudLevelActorMap : public FSpudStructMapData<FString /* FName String */, FSpudNamedObjectData>
{
	virtual const char* GetMagic() const override { return SPUDDATA_LEVELACTORLIST_MAGIC; }
	virtual const char* GetChildMagic() const override { return SPUDDATA_NAMEDOBJECT_MAGIC; }
};

struct FSpudSpawnedActorMap : public FSpudStructMapData<FString /*GUID String*/, FSpudSpawnedActorData>
{
	virtual const char* GetMagic() const override { return SPUDDATA_SPAWNEDACTORLIST_MAGIC; }
	virtual const char* GetChildMagic() const override { return SPUDDATA_SPAWNEDACTOR_MAGIC; }
};
struct FSpudDestroyedActorArray : public FSpudArray<FSpudDestroyedLevelActor>
{
	virtual const char* GetMagic() const override { return SPUDDATA_DESTROYEDACTORLIST_MAGIC; }
	virtual const char* GetChildMagic() const override { return SPUDDATA_DESTROYEDACTOR_MAGIC; }

	void Add(const FString& Name);
};

/// Class definition lookup to hold property definitions are only stored once
struct FSpudClassDefinitions : public FSpudArray<FSpudClassDef>
{
	virtual const char* GetMagic() const override { return SPUDDATA_CLASSDEFINITIONLIST_MAGIC; }
	virtual const char* GetChildMagic() const override { return SPUDDATA_CLASSDEF_MAGIC; }	
};

/// Chunk which provides a simple unique lookup for primitive types, to reduce duplication for various things
/// This is kind of like FName but dedicated to each save file so indexes are compact
template <typename T>
struct FSpudIndex : public FSpudChunk
{
	TMap<T, uint32> Lookup;
	TArray<T> UniqueValues;

	uint32 GetIndex(const T& V) const
	{
		const uint32* pIndex = Lookup.Find(V);
		if (!pIndex)
			return SPUDDATA_INDEX_NONE;

		return *pIndex;
	}

	uint32 FindOrAddIndex(const T& V)
	{
		const uint32* pIndex = Lookup.Find(V);
		if (pIndex)
			return *pIndex;

		uint32 NewIndex = UniqueValues.Num();
		UniqueValues.Add(V);
		Lookup.Add(V, NewIndex);
		return NewIndex;
	}

	const T& GetValue(uint32 Index) const
	{
		check(Index < static_cast<uint32>(UniqueValues.Num()));
		return UniqueValues[Index];
	}

	void Empty()
	{
		Lookup.Empty();
		UniqueValues.Empty();
	}

	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override
	{
		if (ChunkStart(Ar))
		{
			// We only store the array of values
			// Technically dupes some data because TArray self-describes length but convenient
			Ar << UniqueValues;
			ChunkEnd(Ar);
		}
	}

	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 DataVersion) override
	{
		if (ChunkStart(Ar))
		{
			Empty();
			Ar << UniqueValues;
			// Build the lookup
			uint32 Num = static_cast<uint32>(UniqueValues.Num());
			for (uint32 i = 0; i < Num; ++i)
				Lookup.Add(UniqueValues[i], i);
			ChunkEnd(Ar);
		}
	}
	

};

/// Class Name lookup so strings are only stored once in output file
/// Just to save space
struct FSpudClassNameIndex : public FSpudIndex<FString>
{
	virtual const char* GetMagic() const override { return SPUDDATA_CLASSNAMEINDEX_MAGIC; }
};
/// Property Name lookup so strings are only stored once
struct FSpudPropertyNameIndex : public FSpudIndex<FString>
{
	virtual const char* GetMagic() const override { return SPUDDATA_PROPERTYNAMEINDEX_MAGIC; }
};

struct FSpudClassMetadata : public FSpudChunk
{
	/// Description of classes. This allows us to quickly find out what properties are available
	/// in persistent data per class in this saved data, and whether they match the current class def.
	FSpudClassDefinitions ClassDefinitions;
	/// Class Name string -> number index
	/// Again per-level to allow these to get out of sync with each other
	FSpudClassNameIndex ClassNameIndex;
	/// Property Name string -> number index
	/// Again per-level to allow these to get out of sync with each other
	FSpudPropertyNameIndex PropertyNameIndex;

	virtual const char* GetMagic() const override { return SPUDDATA_METADATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 Version) override;

	
	FSpudClassDef& FindOrAddClassDef(const FString& ClassName);
	const FSpudClassDef* GetClassDef(const FString& ClassName) const;
	const FString& GetPropertyNameFromID(uint32 ID) const;
	uint32 FindOrAddPropertyIDFromName(const FString& Name);
	uint32 GetPropertyIDFromName(const FString& Name) const;
	uint32 FindOrAddPropertyIDFromProperty(const FProperty* Prop);
	const FString& GetClassNameFromID(uint32 ID) const;
	uint32 FindOrAddClassIDFromName(const FString& Name);
	uint32 GetClassIDFromName(const FString& Name) const;
	void Reset();
};


struct FSpudLevelData : public FSpudChunk
{
	/// Level Name
	FString Name;
	// this is DELIBERATELY an FString and not an FName because writing FNames to FArchive seems very unreliable
	// it would work fine when writing to an FMemoryWriter but not to an FArchiveFileWriterGeneric (at least using FArchiveProxy)

	/// Class definitions for contents of this level
	/// Per level rather than using global because level data may get out of date. If player doesn't
	/// visit a level for a while and the class def has changed since then we need accurate representation
	/// Level data only gets re-generated when the player goes there
	FSpudClassMetadata Metadata;

	/// Actors which were present in the level at load and which may have modified state
	FSpudLevelActorMap LevelActors;
	/// Actors which were spawned at runtime after the level was loaded (owned by this level)
	FSpudSpawnedActorMap SpawnedActors;
	/// Actors which were present in the level at load time but have been subsequently destroyed
	FSpudDestroyedActorArray DestroyedActors;

	/// Key value for indexing this item; name is unique
	FString Key() const { return Name; }

	virtual const char* GetMagic() const override { return SPUDDATA_LEVELDATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 Version) override;

	/// Empty the lists of actors ready to be re-populated
	virtual void PreStoreWorld();

	void Reset();
};

struct FSpudGlobalData : public FSpudChunk
{
	/// The map name of the level the player was currently on, so we can load back to that point
	FString CurrentLevel;
	/// Class definitions etc for all objects in this global data set
	FSpudClassMetadata Metadata;
	/// Actual storage of object data
	FSpudGlobalObjectMap Objects;

	virtual const char* GetMagic() const override { return SPUDDATA_GLOBALDATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 Version) override;
	void Reset();
};

struct FSpudLevelDataMap : public FSpudStructMapData<FString, FSpudLevelData>
{
	virtual const char* GetMagic() const override { return SPUDDATA_LEVELDATAMAP_MAGIC; }
	virtual const char* GetChildMagic() const override { return SPUDDATA_LEVELDATA_MAGIC; }
};

/// Description of the save game, so we can just read this chunk to get info about it
/// This is better than having a separate metadata file describing the save in order to get description, date/time etc
/// because it means saves can just be copied as single standalone files
struct FSpudSaveInfo : public FSpudChunk
{
	/// The version of the save file, allows us to detect breaking changes which aren't encapsulated as chunks
	uint16 Version;
	/// Title of the save game. Might include things like player region, quest names etc
	FText Title;
	/// Timestamp of the save. Used for display and also to find the latest save for "Continue" behaviour
	FDateTime Timestamp;

	// TODO: support for screenshots and custom data. These should be encapsulated in child chunks
	
	virtual const char* GetMagic() const override { return SPUDDATA_SAVEINFO_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 Version) override;
	
};
/// The top-level structure for the entire save file
struct FSpudSaveData : public FSpudChunk
{
	FSpudSaveInfo Info;
	FSpudGlobalData GlobalData;
	FSpudLevelDataMap LevelDataMap;

	virtual const char* GetMagic() const override { return SPUDDATA_SAVEGAME_MAGIC; }
	void PrepareForWrite(const FText& Title);
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint16 IGNOREDDataVersion) override;
	
	virtual void Reset();

	/// Utility method to read an archive just up to the end of the FSpudSaveInfo, and output details
	static bool ReadSaveInfoFromArchive(FSpudChunkedDataArchive& Ar, FSpudSaveInfo& OutInfo);
};
