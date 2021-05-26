#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpudData, Verbose, Verbose);

extern int32 GCurrentUserDataModelVersion;

// Chunk IDs
#define SPUDDATA_SAVEGAME_MAGIC "SAVE"
#define SPUDDATA_SAVEINFO_MAGIC "INFO"
#define SPUDDATA_SCREENSHOT_MAGIC "SHOT"
// custom per-save header info
#define SPUDDATA_CUSTOMINFO_MAGIC "CINF"
#define SPUDDATA_METADATA_MAGIC "META"
#define SPUDDATA_CLASSDEFINITIONLIST_MAGIC "CLST"
#define SPUDDATA_CLASSDEF_MAGIC "CDEF"
#define SPUDDATA_CLASSNAMEINDEX_MAGIC "CNIX"
#define SPUDDATA_PROPERTYNAMEINDEX_MAGIC "PNIX"
#define SPUDDATA_VERSIONINFO_MAGIC "VERS"
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
// custom per-object data
#define SPUDDATA_CUSTOMDATA_MAGIC "CUST" 
#define SPUDDATA_COREACTORDATA_MAGIC "CORA"

#define SPUDDATA_INDEX_NONE 0xFFFFFFFF
#define SPUDDATA_PROPERTYID_NONE 0xFFFFFFFF
#define SPUDDATA_PREFIXID_NONE 0xFFFFFFFF

// None of the structs in this file are exposed to Blueprints. They are theoretically available to external code
// via C++ but honestly external code should just use the API on USpudSubsystem, or USpudState at a push (save upgrading)

// Serialized data format is binary chunk-based, inspired by IFF (like lots of things!)
// For efficiency not everything is a separate chunk, the idea is that if anything breaking changes within a chunk
// then the chunk ID can be changed, and fallback load paths provided for older deprecated chunks.
// The top-level save file has a version number as well but this is mostly for info or very serious changes which require
// explicit upgrading

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

enum SPUD_API ESpudStorageType // (stored as uint16 but not using enum class to make bitwise ops easier)
{
	// All of these are serialised as per their underlying types
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
struct SPUD_API FSpudChunkHeader
{

	uint32 Magic; // Identifier
	uint32 Length; // Excluding header, including nested data

	static constexpr int64 GetHeaderSize() { return sizeof(uint32) + sizeof(uint32); }

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

struct SPUD_API FSpudChunkedDataArchive : public FArchiveProxy
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

struct SPUD_API FSpudChunk
{
	FSpudChunkHeader ChunkHeader;
	int64 ChunkHeaderStart;
	int64 ChunkDataStart;
	/// The end of this data; only valid when reading.
	int64 ChunkDataEnd;
	
	virtual ~FSpudChunk() = default;
	virtual const char* GetMagic() const = 0;
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) = 0;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) = 0;

	bool ChunkStart(FArchive& Ar);
	void ChunkEnd(FArchive& Ar);
	bool IsStillInChunk(FArchive& Ar) const;
};

// An ad-hoc chunk used to wrap other chunks. 
struct SPUD_API FSpudAdhocWrapperChunk : public FSpudChunk
{
	const char* Magic;
	
	FSpudAdhocWrapperChunk(const char* InMagic) : Magic(InMagic) {}
	virtual const char* GetMagic() const override { return Magic; }
	// You should not call read/write, this chunk is solely for wrapping others without owning them
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override { check(false);}
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override { check(false); }
};

/// Definition of a property on a class
/// We store a list of these for each known leaf class
/// Each instance will store related offsets within its data buffer (those can be variable because
/// of variable-size properties, like strings and arrays)
struct SPUD_API FSpudPropertyDef
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

struct SPUD_API FSpudVersionInfo : public FSpudChunk
{
	// Signed for blueprint compat (user version might be set from BP)
	int32 Version;


	virtual const char* GetMagic() const override { return SPUDDATA_VERSIONINFO_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
};


/// Definition of a class, to share property definitions
struct SPUD_API FSpudClassDef : public FSpudChunk
{
	FString ClassName;
	
	/// Map from PrefixID -> Map of PropertyNameID to property definition index
	TMap<uint32, // Prefix ID
		TMap<uint32, int>> PropertyLookup; // Property Name ID -> Index
	/// Actual property storage, these indexes are what actual instances store offsets against
	TArray<FSpudPropertyDef> Properties;

	virtual const char* GetMagic() const override{ return SPUDDATA_CLASSDEF_MAGIC; }
	
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;

	/// Add a new property and return its index
	int AddProperty(uint32 InPropNameID, uint32 InPrefixID, uint16 InDataType);
	/// Find a property or null. IDs are from PropertyNameIndex
	const FSpudPropertyDef* FindProperty(uint32 PropNameID, uint32 PrefixID);
	/// Find a property index or -1. IDs are from PropertyNameIndex
	int FindPropertyIndex(uint32 PropNameID, uint32 PrefixID);
	/// Find a property index or add it if missing. IDs are from PropertyNameIndex
	int FindOrAddPropertyIndex(uint32 PropNameID, uint32 PrefixID, uint16 DataType);
	bool RenameProperty(uint32 OldPropID, uint32 OldPrefixID, uint32 NewPropID, uint32 NewPrefixID);

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
struct SPUD_API FSpudDataHolder : public FSpudChunk
{
	TArray<uint8> Data;

	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;

	virtual void Reset();

};
/// Holder for property data that we automatically populate
struct SPUD_API FSpudPropertyData : public FSpudChunk
{
	/// List of byte offsets into the data buffer where each property can be found
	/// Properties are ordered as per the relevant FSpudClassDef
	/// We need offsets per-instance not per-class because data offsets can be different per instance
	// (string lengths, array lengths can vary)
	TArray<uint32> PropertyOffsets;
	TArray<uint8> Data;
	
	virtual const char* GetMagic() const override { return SPUDDATA_PROPERTYDATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
	void ReadFromArchiveV1(FSpudChunkedDataArchive& Ar);

	virtual void Reset();

};
/// Holder for actor core data; not properties e.g. transform
struct SPUD_API FSpudCoreActorData : public FSpudDataHolder
{
	virtual const char* GetMagic() const override { return SPUDDATA_COREACTORDATA_MAGIC; }
};
/// Holder for any custom data you want to store on top of SaveGame properties
struct SPUD_API FSpudCustomData : public FSpudDataHolder
{
	virtual const char* GetMagic() const override { return SPUDDATA_CUSTOMDATA_MAGIC; }
};

// Abstract general def of object data
struct SPUD_API FSpudObjectData : public FSpudChunk
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
struct SPUD_API FSpudNamedObjectData : public FSpudObjectData
{
	FString Name;

	/// Key value for indexing this item; name is unique in the level
	FString Key() const { return Name; }

	virtual const char* GetMagic() const override { return SPUDDATA_NAMEDOBJECT_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
};

/// An actor which was spawned at runtime, not present in a level
struct SPUD_API FSpudSpawnedActorData : public FSpudObjectData
{
	uint32 ClassID; // ID for the ClassName (see FSpudClassNameIndex) 
	FGuid Guid;

	/// Key value for indexing this item; name is unique in the level
	FString Key() const { return Guid.ToString(SPUDDATA_GUID_KEY_FORMAT); }

	virtual const char* GetMagic() const override { return SPUDDATA_SPAWNEDACTOR_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
};


struct SPUD_API FSpudDestroyedLevelActor : public FSpudChunk
{
	FString Name;

	/// Key value for indexing this item; name is unique in the level
	FString Key() const { return Name; }

	FSpudDestroyedLevelActor() {}
	FSpudDestroyedLevelActor(const FString& InName) : Name(InName) {}

	virtual const char* GetMagic() const override { return SPUDDATA_DESTROYEDACTOR_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
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

	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override
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
					ChildData.ReadFromArchive(Ar, StoredSystemVersion);
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

	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override
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
					ChildData.ReadFromArchive(Ar, StoredSystemVersion);
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

struct FSpudNamedObjectMap : public FSpudStructMapData<FString /* FName or Guid String */, FSpudNamedObjectData>
{
	virtual bool RenameObject(const FString& OldName, const FString& NewName);
};

struct FSpudGlobalObjectMap : public FSpudNamedObjectMap
{
	virtual const char* GetMagic() const override { return SPUDDATA_GLOBALOBJECTLIST_MAGIC; }
	virtual const char* GetChildMagic() const override { return SPUDDATA_NAMEDOBJECT_MAGIC; }
};
struct FSpudLevelActorMap : public FSpudNamedObjectMap
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

	uint32 Rename(const T& Old, const T& New)
	{
		uint32 Index;
		if (Lookup.RemoveAndCopyValue(Old, Index))
		{
			Lookup.Add(New, Index);
			return Index;
		}

		return SPUDDATA_INDEX_NONE;
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

	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override
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

struct SPUD_API FSpudClassMetadata : public FSpudChunk
{
	/// Description of classes. This allows us to quickly find out what properties are available
	/// in persistent data per class in this saved data, and whether they match the current class def.
	FSpudClassDefinitions ClassDefinitions;
	/// Class Name string -> number index
	FSpudClassNameIndex ClassNameIndex;
	/// Property Name string -> number index (also used for prefixes, but prefix and property name are separate to help name re-use)
	FSpudPropertyNameIndex PropertyNameIndex;

	/// The user data model version number when this metadata was generated
	/// @see USpudSubsystem::SetUserDataModelVersion
	FSpudVersionInfo UserDataModelVersion;
	
	virtual const char* GetMagic() const override { return SPUDDATA_METADATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;

	
	FSpudClassDef& FindOrAddClassDef(const FString& ClassName);
	const FSpudClassDef* GetClassDef(const FString& ClassName) const;
	const FString& GetPropertyNameFromID(uint32 ID) const;
	uint32 FindOrAddPropertyIDFromName(const FString& Name);
	uint32 GetPropertyIDFromName(const FString& Name) const;
	uint32 FindOrAddPropertyIDFromProperty(const FProperty* Prop);
	uint32 FindOrAddPrefixID(const FString& Prefix);
	uint32 GetPrefixID(const FString& Prefix);
	const FString& GetClassNameFromID(uint32 ID) const;
	uint32 FindOrAddClassIDFromName(const FString& Name);
	uint32 GetClassIDFromName(const FString& Name) const;
	void Reset();

	
	bool RenameClass(const FString& OldClassName, const FString& NewClassName);
	bool RenameProperty(const FString& ClassName, const FString& OldName, const FString& NewName, const FString& OldPrefix = "", const FString& NewPrefix = "");
	
	bool IsUserDataModelOutdated() const { return UserDataModelVersion.Version != GCurrentUserDataModelVersion; }
	uint32 GetUserDataModelVersion() const { return UserDataModelVersion.Version; }
};

enum SPUD_API ELevelDataStatus
{
	LDS_Unloaded,
	LDS_BackgroundWriteAndUnload,
	LDS_Loaded
};

struct SPUD_API FSpudGlobalData : public FSpudChunk
{

	/// The map name of the level the player was currently on, so we can load back to that point
	FString CurrentLevel;
	/// Class definitions etc for all objects in this global data set
	FSpudClassMetadata Metadata;
	/// Actual storage of object data
	FSpudGlobalObjectMap Objects;

	virtual const char* GetMagic() const override { return SPUDDATA_GLOBALDATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
	void Reset();
	
	bool IsUserDataModelOutdated() const { return Metadata.IsUserDataModelOutdated(); }
	uint32 GetUserDataModelVersion() const { return Metadata.GetUserDataModelVersion(); }
};

struct SPUD_API FSpudLevelData : public FSpudChunk
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

	/// non-persistent status flag to support placeholder level data which is not currently loaded
	ELevelDataStatus Status;
	/// Mutex for the data in this level. You should lock this before altering any contents because levels can
	/// be loaded in multiple threads
	FCriticalSection Mutex;
	/// Thread-safe check if this level data is currently loaded
	bool IsLoaded();
	/// Release the memory associated with this level but keep basic data like Name
	void ReleaseMemory();
	
	/// Key value for indexing this item; name is unique
	FString Key() const { return Name; }

	FSpudLevelData() {}

	// We need an explicit copy constructor in order to not try to copy the mutex
	FSpudLevelData(const FSpudLevelData& Other)
		: FSpudChunk(Other),
		  Name(Other.Name),
		  Metadata(Other.Metadata),
		  LevelActors(Other.LevelActors),
		  SpawnedActors(Other.SpawnedActors),
		  DestroyedActors(Other.DestroyedActors),
		  Status(Other.Status)
	{
	}

	virtual const char* GetMagic() const override { return SPUDDATA_LEVELDATA_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;

	/// Empty the lists of actors ready to be re-populated
	virtual void PreStoreWorld();

	/// Read just enough of the next level chunk to retrieve the name, then optionally return the read pointer to where it was
	static bool ReadLevelInfoFromArchive(FSpudChunkedDataArchive& Ar, bool bReturnToStart, FString& OutLevelName, int64& OutDataSize);

	void Reset();

	bool IsUserDataModelOutdated() const { return Metadata.IsUserDataModelOutdated(); }
	uint32 GetUserDataModelVersion() const { return Metadata.GetUserDataModelVersion(); }
};

/// Screenshot chunk
struct SPUD_API FSpudScreenshot : public FSpudChunk
{
	// PNG encoded image bytes
	TArray<uint8> ImageData;
	
	virtual const char* GetMagic() const override { return SPUDDATA_SCREENSHOT_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
};


/// Custom information you can store at the front of a save file. Used to store anything else that you want to be able
/// to display on save/load screens that isn't already covered by FSpudSaveInfo.
struct SPUD_API FSpudSaveCustomInfo : public FSpudChunk
{
	// This uses a lot of the same concepts as object storage elsewhere in SPUD, but is simplified since we only
	// need a single set of properties here and always use the "slow" lookup route (since this will never occur
	// at scale). So there is no class def lookup, no shared property indexes or name indexes. But the property
	// storage is basically the same.
	
	/// List of property names ordered by position in the data buffer (not a map because this is simpler and more useful for insertion)
	/// We shouldn't have enough data in here to make a difference performance wise
	TArray<FString> PropertyNames;
	/// Ordered list of data offsets corresponding to names
	TArray<uint32> PropertyOffsets;
	/// Property storage, consists of a block of data and offset metadata (indexed)  
	TArray<uint8> PropertyData;

	virtual const char* GetMagic() const override { return SPUDDATA_CUSTOMINFO_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
	void Reset();
};

/// Description of the save game, so we can just read this chunk to get info about it
/// This is better than having a separate metadata file describing the save in order to get description, date/time etc
/// because it means saves can just be copied as single standalone files
struct SPUD_API FSpudSaveInfo : public FSpudChunk
{
	
	/// The SYSTEM version this save file belongs to. This number will only change when breaking changes are made to the
	/// SPUD system itself which need to be reflected at the top level. Most changes will be contained in extra chunks
	/// to avoid this and SPUD is responsible for upgrading saves when this changes.
	uint16 SystemVersion;
	/// Title of the save game. Might include things like player region, quest names etc
	FText Title;
	/// Timestamp of the save. Used for display and also to find the latest save for "Continue" behaviour
	FDateTime Timestamp;
	/// Custom fields to be made available in info header
	FSpudSaveCustomInfo CustomInfo;
	/// Optional screenshot
	FSpudScreenshot Screenshot;

	// TODO: support custom data. Should be encapsulated in child chunk(s)
	
	virtual const char* GetMagic() const override { return SPUDDATA_SAVEINFO_MAGIC; }
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;
	
	void Reset();
};

/// The top-level structure for the entire save file
struct SPUD_API FSpudSaveData : public FSpudChunk
{

	FSpudSaveInfo Info;
	FSpudGlobalData GlobalData;

	// Plain map for level data, because we can page this out so don't write it in bulk
	// Also we want threadsafe shared ptr for data holder so that we can write it in the background without holding the
	// lock on the entire map while we do so
	typedef TSharedPtr<FSpudLevelData, ESPMode::ThreadSafe> TLevelDataPtr;
	TMap<FString, TLevelDataPtr> LevelDataMap;
	// Mutex for altering the level data map
	FCriticalSection LevelDataMapMutex;

	virtual const char* GetMagic() const override { return SPUDDATA_SAVEGAME_MAGIC; }
	void PrepareForWrite();
	/// Write the entire in-memory contents to a singe archive, assumes all data is in memory
	virtual void WriteToArchive(FSpudChunkedDataArchive& Ar) override;
	/// Write contents to archive, with the option of loading back in level data that's been released (doesn't all have to be in memory)
	void WriteToArchive(FSpudChunkedDataArchive& Ar, const FString& LevelPath);
	/// Read the entire save file into memory
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion) override;

	/**
	 * @brief Read a save file with extra options. Options to pipe all level data chunks
	 * directly into their own separate named files in LevelPath, so their state is only loaded into memory when
	 * needed. Entries for these levels will still be present in LevelDataMap, but with an unloaded state
	 * @param Ar Source archive for the entire save file
	 * @param bLoadAllLevels If true, all levels will be loaded into memory. If false, none will be & data will be split for later loading
	 * @param LevelPath The parent directory where level chunks should be written as separate files
	 */
	virtual void ReadFromArchive(FSpudChunkedDataArchive& Ar, bool bLoadAllLevels, const FString& LevelPath);
	
	/**
	 * @brief Retrieve data for a single level, loading it if necessary. Thread-safe.
	 * @param LevelName The name of the level
	 * @param bLoadIfNeeded Load (synchronously) if the level is present but unloaded
	 * @param LevelPath The parent directory where level chunks can be found as separate files
	 * @return The level data or null if not available
	 */
	virtual TLevelDataPtr GetLevelData(const FString& LevelName, bool bLoadIfNeeded, const FString& LevelPath);

	
	/**
	 * @brief Create level data for a new level
	 * @param LevelName The name of the level
	 * @return Pointer to the new level data
	 */
	virtual TLevelDataPtr CreateLevelData(const FString& LevelName);


	/**
	* @brief Write any loaded data for a single level to disk, and unload it from memory . It becomes part of the
	* on-disk state for the active game which can later be re-combined with others into a single save game.
	* @param LevelName The name of the level
    * @param LevelPath The path in which to write the level data
	*/
	virtual bool WriteAndReleaseLevelData(const FString& LevelName, const FString& LevelPath, bool bBlocking);
	
	/**
	* @brief Write any loaded data for all levels to disk, and unload from memory . They become part of the
	* on-disk state for the active game which can later be re-combined with others into a single save game.
	* @param LevelPath The path in which to write the level data
	*/
	virtual void WriteAndReleaseAllLevelData(const FString& LevelPath);
	/**
	 * @brief Delete any state associated with a given level, forgetting any saved state for it.
	 * @param LevelName The name of the level
	 * @param LevelPath The path in which paged out level data may have been written
	 */
	virtual void DeleteLevelData(const FString& LevelName, const FString& LevelPath);

	virtual void Reset();

	/// Remove all the level data files in a given path
	static void DeleteAllLevelDataFiles(const FString& LevelPath);

	/// Get the path of the file to use to store state for a specific level
	static FString GetLevelDataPath(const FString& LevelPath, const FString& LevelName);

	/// Write Level Data to disk
	static void WriteLevelData(FSpudLevelData& LevelData, const FString& LevelName, const FString& LevelPath);

	/// Utility method to read an archive just up to the end of the FSpudSaveInfo, and output details
	static bool ReadSaveInfoFromArchive(FSpudChunkedDataArchive& Ar, FSpudSaveInfo& OutInfo);
};


/**
 * @brief Copy bytes from one archive to another (can't seem to find a built-in way to do this?)
 * @param InArchive Archive to read from
 * @param OutArchive Archive to write to
 * @param Length Total length of data to copy
 * @return The length of the data actually copied
 */
int64 SpudCopyArchiveData(FArchive& InArchive, FArchive& OutArchive, int64 Length);
