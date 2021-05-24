#include "SpudData.h"

#include <algorithm>

#include "SpudPropertyUtil.h"

DEFINE_LOG_CATEGORY(LogSpudData)

// System version covers our internal format changes
#define SPUD_CURRENT_SYSTEM_VERSION 2

// int32 so that Blueprint-compatible. 2 billion should be enough anyway and you can always use the negatives
int32 GCurrentUserDataModelVersion = 0;
//------------------------------------------------------------------------------

bool FSpudChunkedDataArchive::PreviewNextChunk(FSpudChunkHeader& OutHeader, bool SeekBackToHeader)
{
	if (!IsLoading())
		return false;

	const int64 CurrPos = Tell();

	if ((CurrPos + FSpudChunkHeader::GetHeaderSize()) > TotalSize())
		return false;

	*this << OutHeader;

	if (SeekBackToHeader)
		Seek(CurrPos);

	return true;
}

bool FSpudChunkedDataArchive::NextChunkIs(uint32 EncodedMagic)
{
	FSpudChunkHeader Header;
	if (PreviewNextChunk(Header))
		return Header.Magic == EncodedMagic;

	return false;
}

bool FSpudChunkedDataArchive::NextChunkIs(const char* Magic)
{
	return NextChunkIs(FSpudChunkHeader::EncodeMagic(Magic));
}

void FSpudChunkedDataArchive::SkipNextChunk()
{
	// only valid when loading
	if (!IsLoading())
	{
		UE_LOG(LogSpudData, Fatal, TEXT("Invalid to call SkipNextChunk when writing"))
		return;
	}

	FSpudChunkHeader Header;
	if (PreviewNextChunk(Header, false))
	{
		// Length is after header so we can just seek from here
		Seek(Tell() + Header.Length);
	}
	else
	{
		UE_LOG(LogSpudData, Fatal, TEXT("Unable to preview next chunk to skip"))
		return;
	}
}
//------------------------------------------------------------------------------

bool FSpudChunk::ChunkStart(FArchive& Ar)
{
	ChunkHeaderStart = Ar.Tell();
	if (Ar.IsLoading())
	{
		Ar << ChunkHeader;

		if (FSpudChunkHeader::EncodeMagic(GetMagic()) != ChunkHeader.Magic)
		{
			// Incorrect chunk, seek back
			Ar.Seek(ChunkHeaderStart);
			return false;
		}

		ChunkDataStart = Ar.Tell();
		ChunkDataEnd = ChunkDataStart + ChunkHeader.Length;
	}
	else
	{
		// We fill length in properly later
		ChunkHeader.Set(GetMagic(), 0);
		Ar << ChunkHeader;
		ChunkDataStart = Ar.Tell();
	}
	return true;
}

void FSpudChunk::ChunkEnd(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		// Reading, make sure we skip any nested chunks we don't understand at this version
		if (Ar.Tell() != ChunkDataEnd)
			Ar.Seek(ChunkDataEnd);
	}
	else
	{
		// Writing
		// Finalise the header, include length
		int64 CurrentPos = Ar.Tell();
		ChunkDataEnd = CurrentPos;
		ChunkHeader.Length = ChunkDataEnd - ChunkDataStart;
		Ar.Seek(ChunkHeaderStart);
		Ar << ChunkHeader;
		Ar.Seek(CurrentPos);
	}
}

bool FSpudChunk::IsStillInChunk(FArchive& Ar) const
{
	if (Ar.IsLoading())
	{
		return Ar.Tell() < ChunkDataEnd;
	}
	else
		return true; // always inside while writing
}
//------------------------------------------------------------------------------

void FSpudVersionInfo::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	// Separate chunk for version info is a bit indulgent but it means we can tack it on to anything if we want
	if (ChunkStart(Ar))
	{
		Ar << Version;
		ChunkEnd(Ar);
	}
}
void FSpudVersionInfo::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		Ar << Version;		
		ChunkEnd(Ar);
	}
}

//------------------------------------------------------------------------------
void FSpudClassDef::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << ClassName;
		// Convert our map to a flat structure
		// We won't use chunks for each child struct, so write the length first
		uint16 NumProperties = static_cast<uint16>(Properties.Num());
		Ar << NumProperties;
		for (auto && Def : Properties)
		{
			Ar << Def.PropertyID;
			Ar << Def.PrefixID;
			Ar << Def.DataType;
		}
		ChunkEnd(Ar);
	}	
}

void FSpudClassDef::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		Ar << ClassName;
		// Convert from a flat array to a map
		// Length was first
		uint16 NumProperties;
		Ar << NumProperties;
		Properties.Empty();
		PropertyLookup.Empty();
		for (uint16 i = 0; i < NumProperties; ++i)
		{
			uint32 PropertyID;
			uint32 PrefixID;
			uint16 DataType;
			Ar << PropertyID;
			Ar << PrefixID;
			Ar << DataType;

			AddProperty(PropertyID, PrefixID, DataType);
		}
		RuntimeMatchState = NotChecked;
		ChunkEnd(Ar);
	}	
}

int FSpudClassDef::AddProperty(uint32 InPropNameID, uint32 InPrefixID, uint16 InDataType)
{
	int Index = Properties.Num(); 
	Properties.Add(FSpudPropertyDef(InPropNameID, InPrefixID, InDataType));

	auto& InnerMap = PropertyLookup.FindOrAdd(InPrefixID);
	InnerMap.Add(InPropNameID, Index);

	return Index;
}

const FSpudPropertyDef* FSpudClassDef::FindProperty(uint32 PropNameID, uint32 PrefixID)
{
	int Index = FindPropertyIndex(PropNameID, PrefixID);
	if (Index < 0)
		return nullptr;
	
	return &Properties[Index];
	
}

int FSpudClassDef::FindPropertyIndex(uint32 PropNameID, uint32 PrefixID)
{
	auto InnerMap = PropertyLookup.Find(PrefixID);
	if (!InnerMap)
		return -1;

	int* pIndex = InnerMap->Find(PropNameID);
	if (!pIndex)
		return -1;

	return *pIndex;
}

int FSpudClassDef::FindOrAddPropertyIndex(uint32 PropNameID, uint32 PrefixID, uint16 DataType)
{
	const int Index = FindPropertyIndex(PropNameID, PrefixID);
	if (Index >= 0)
		return Index;

	return AddProperty(PropNameID, PrefixID, DataType);
}

bool FSpudClassDef::RenameProperty(uint32 OldPropID, uint32 OldPrefixID, uint32 NewPropID, uint32 NewPrefixID)
{
	const int Index = FindPropertyIndex(OldPropID, OldPrefixID);
	if (Index >= 0)
	{
		auto& Propdef = Properties[Index];
		Propdef.PrefixID = NewPrefixID;
		Propdef.PropertyID = NewPropID;

		// We can't just rename the outer lookup because it may just be one of many properties that have moved
		auto& OldInnerMap = PropertyLookup.FindChecked(OldPrefixID);
		OldInnerMap.Remove(OldPropID);

		auto& NewInnerMap = PropertyLookup.FindOrAdd(NewPrefixID);
		NewInnerMap.Add(NewPropID, Index);

		return true;
		
	}

	return false;
}

bool FSpudClassDef::MatchesRuntimeClass(const FSpudClassMetadata& Meta) const
{
	if (RuntimeMatchState == NotChecked)
	{
		// Run through the actual code class properties the same way
		RuntimeMatchState = SpudPropertyUtil::StoredClassDefMatchesRuntime(*this, Meta) ?
			Matching : Different;
		
	}
	return RuntimeMatchState == Matching;
	
}

//------------------------------------------------------------------------------

void FSpudPropertyData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << PropertyOffsets;
		Ar << Data;
		ChunkEnd(Ar);
	}
}

void FSpudPropertyData::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	// Breaking change compat
	if (StoredSystemVersion == 1)
	{
		ReadFromArchiveV1(Ar);
		return;
	}

	// Latest system version
	PropertyOffsets.Empty();
	if (ChunkStart(Ar))
	{
		Ar << PropertyOffsets;
		Ar << Data;
		ChunkEnd(Ar);
	}
}
void FSpudPropertyData::ReadFromArchiveV1(FSpudChunkedDataArchive& Ar)
{
	// V1 accidentally wrote PropertyOffsets *before* the chunk header because it wrote it
	// manually then re-used the parent class write. 
	// It all worked because read & write were the same, but it breaks the rules of chunk wrapping
	// We need to read this back the old way for compatibility

	PropertyOffsets.Empty();
	Ar << PropertyOffsets;
	// This bit used to be a call to inherited Read, hence wrapping incorrectly
	if (ChunkStart(Ar))
	{
		Ar << Data;
		ChunkEnd(Ar);
	}	
}

void FSpudPropertyData::Reset()
{
	PropertyOffsets.Empty();
	Data.Empty();
}

//------------------------------------------------------------------------------

void FSpudDataHolder::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	// Only write this chunk if there's some data
	if (Data.Num() == 0)
		return;
	
	if (ChunkStart(Ar))
	{
		// Technically this duplicates some information, since TArray writes the length of the array at the
		// start and we already wrote the chunk length (4 bytes longer). But this makes everything simpler,
		// our length for chunk skipping, and TArray's length for normal serialisation.
		Ar << Data;
		ChunkEnd(Ar);
	}
}
void FSpudDataHolder::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		Ar << Data;
		ChunkEnd(Ar);
	}
}

void FSpudDataHolder::Reset()
{
	Data.Empty();
}

//------------------------------------------------------------------------------

void FSpudDestroyedLevelActor::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << Name;
		ChunkEnd(Ar);
	}
}

void FSpudDestroyedLevelActor::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	// Simple case
	if (ChunkStart(Ar))
	{
		Ar << Name;
		ChunkEnd(Ar);
	}
}

//------------------------------------------------------------------------------
void FSpudNamedObjectData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << Name;
		CoreData.WriteToArchive(Ar);
		Properties.WriteToArchive(Ar);
		CustomData.WriteToArchive(Ar);
		ChunkEnd(Ar);
	}
}

void FSpudNamedObjectData::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		Ar << Name;
		CoreData.ReadFromArchive(Ar, StoredSystemVersion);
		Properties.ReadFromArchive(Ar, StoredSystemVersion);
		CustomData.ReadFromArchive(Ar, StoredSystemVersion);
		ChunkEnd(Ar);
	}
}
//------------------------------------------------------------------------------
void FSpudSpawnedActorData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << ClassID;
		Ar << Guid;
		CoreData.WriteToArchive(Ar);
		Properties.WriteToArchive(Ar);
		CustomData.WriteToArchive(Ar);
		ChunkEnd(Ar);
	}
}

void FSpudSpawnedActorData::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		Ar << ClassID;
		Ar << Guid;
		CoreData.ReadFromArchive(Ar, StoredSystemVersion);
		Properties.ReadFromArchive(Ar, StoredSystemVersion);
		CustomData.ReadFromArchive(Ar, StoredSystemVersion);
		ChunkEnd(Ar);
	}
}

//------------------------------------------------------------------------------

bool FSpudNamedObjectMap::RenameObject(const FString& OldName, const FString& NewName)
{
	FSpudNamedObjectData ObjData;
	if(Contents.RemoveAndCopyValue(OldName, ObjData))
	{
		ObjData.Name = NewName;
		Contents.Add(NewName, ObjData);
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------

void FSpudDestroyedActorArray::Add(const FString& Name)
{

	Values.Add(FSpudDestroyedLevelActor(Name));
	
}
//------------------------------------------------------------------------------

void FSpudClassMetadata::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		UserDataModelVersion.Version = GCurrentUserDataModelVersion;
		UserDataModelVersion.WriteToArchive(Ar);
		
		ClassNameIndex.WriteToArchive(Ar);
		ClassDefinitions.WriteToArchive(Ar);
		PropertyNameIndex.WriteToArchive(Ar);

		ChunkEnd(Ar);
	}
}

void FSpudClassMetadata::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		const uint32 VersionID = FSpudChunkHeader::EncodeMagic(SPUDDATA_VERSIONINFO_MAGIC);
		const uint32 ClassNameIndexID = FSpudChunkHeader::EncodeMagic(SPUDDATA_CLASSNAMEINDEX_MAGIC);
		const uint32 ClassDefListID = FSpudChunkHeader::EncodeMagic(SPUDDATA_CLASSDEFINITIONLIST_MAGIC);
		const uint32 PropertyNameIndexID = FSpudChunkHeader::EncodeMagic(SPUDDATA_PROPERTYNAMEINDEX_MAGIC);
		FSpudChunkHeader Hdr;
		while (IsStillInChunk(Ar))
		{
			Ar.PreviewNextChunk(Hdr, true);
			if (Hdr.Magic == VersionID)
				UserDataModelVersion.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == ClassNameIndexID)
				ClassNameIndex.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == ClassDefListID)
				ClassDefinitions.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == PropertyNameIndexID)
				PropertyNameIndex.ReadFromArchive(Ar, StoredSystemVersion);
			else
				Ar.SkipNextChunk();
		}
		ChunkEnd(Ar);
	}
}

FSpudClassDef& FSpudClassMetadata::FindOrAddClassDef(const FString& ClassName)
{
	// This adds new entries
	int Index = ClassNameIndex.FindOrAddIndex(ClassName);
	if (ClassDefinitions.Values.Num() < Index + 1)
	{
		ClassDefinitions.Values.SetNum(Index + 1);
		auto& Def = ClassDefinitions.Values[Index];
		Def.ClassName = ClassName;
	}
	return ClassDefinitions.Values[Index];
	
}
const FSpudClassDef* FSpudClassMetadata::GetClassDef(const FString& ClassName) const
{
	int Index = ClassNameIndex.GetIndex(ClassName);
	if (Index != SPUDDATA_INDEX_NONE)
	{
		return &ClassDefinitions.Values[Index];
	}
	return nullptr;
}

const FString& FSpudClassMetadata::GetPropertyNameFromID(uint32 ID) const
{
	return PropertyNameIndex.GetValue(ID);
}
uint32 FSpudClassMetadata::FindOrAddPropertyIDFromName(const FString& Name)
{
	return PropertyNameIndex.FindOrAddIndex(Name);
}

uint32 FSpudClassMetadata::GetPropertyIDFromName(const FString& Name) const
{
	return PropertyNameIndex.GetIndex(Name);
}

uint32 FSpudClassMetadata::FindOrAddPropertyIDFromProperty(const FProperty* Prop)
{
	return FindOrAddPropertyIDFromName(Prop->GetNameCPP());
}
uint32 FSpudClassMetadata::FindOrAddPrefixID(const FString& Prefix)
{
	// Special case blank
	if (Prefix.IsEmpty())
		return SPUDDATA_PREFIXID_NONE;

	// Otherwise same as property (helps share names when scope & prop names are their own entries)
	return FindOrAddPropertyIDFromName(Prefix);	
}
uint32 FSpudClassMetadata::GetPrefixID(const FString& Prefix)
{
	// Special case blank
	if (Prefix.IsEmpty())
		return SPUDDATA_PREFIXID_NONE;
	
	// Prefixes share the property name lookup
	return GetPropertyIDFromName(Prefix);	
}

const FString& FSpudClassMetadata::GetClassNameFromID(uint32 ID) const
{
	return ClassNameIndex.GetValue(ID);
}
uint32 FSpudClassMetadata::FindOrAddClassIDFromName(const FString& Name)
{
	return ClassNameIndex.FindOrAddIndex(Name);
}
uint32 FSpudClassMetadata::GetClassIDFromName(const FString& Name) const
{
	return ClassNameIndex.GetIndex(Name);
}

void FSpudClassMetadata::Reset()
{
	ClassDefinitions.Reset();
	PropertyNameIndex.Empty();
	ClassNameIndex.Empty();	
}

bool FSpudClassMetadata::RenameClass(const FString& OldClassName, const FString& NewClassName)
{
	uint32 Index = ClassNameIndex.Rename(OldClassName, NewClassName);
	if (Index != SPUDDATA_INDEX_NONE)
	{
		auto& ClassDef = ClassDefinitions.Values[Index];
		ClassDef.ClassName = NewClassName;
		return true;
	}
	return false;
}

bool FSpudClassMetadata::RenameProperty(const FString& ClassName, const FString& OldName, const FString& NewName, const FString& OldPrefix, const FString& NewPrefix)
{
	uint32* pClassID = ClassNameIndex.Lookup.Find(ClassName);
	uint32* pPropertyNameID = PropertyNameIndex.Lookup.Find(OldName);
	if (pClassID && pPropertyNameID)
	{
		// Need to find or add a new name IDs since prop names can be used across many classes
		// This may orphan the old name ID but that doesn't hurt anyone except consuming a few bytes

		// Now point our property for that class at new name. Everything else remains the same
		auto& Def = ClassDefinitions.Values[*pClassID];
	
		uint32 OldNameID = GetPropertyIDFromName(OldName);
		uint32 NewNameID = FindOrAddPropertyIDFromName(NewName);
		uint32 OldPrefixID = GetPrefixID(OldPrefix);
		uint32 NewPrefixID = FindOrAddPrefixID(NewPrefix);

		return Def.RenameProperty(OldNameID, OldPrefixID, NewNameID, NewPrefixID);
	}

	return false;
	
}

//------------------------------------------------------------------------------
void FSpudLevelData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	FScopeLock Lock(&Mutex);

	if (Status == LDS_Unloaded)
	{
		UE_LOG(LogSpudData, Error, TEXT("Attempted to write an unloaded LevelData struct for %s, skipping"), *Name);
		return;
	}

	if (ChunkStart(Ar))
	{
		Ar << Name;
		Metadata.WriteToArchive(Ar);
		LevelActors.WriteToArchive(Ar);
		SpawnedActors.WriteToArchive(Ar);
		DestroyedActors.WriteToArchive(Ar);
		ChunkEnd(Ar);
	}
}

bool FSpudLevelData::ReadLevelInfoFromArchive(FSpudChunkedDataArchive& Ar, bool bReturnToStart, FString& OutLevelName, int64& OutDataSize)
{
	// No lock needed as we're not populating anything, this method can  be static
	// Do part of ChunkStart required to read header
	if (!Ar.IsLoading())
	{
		UE_LOG(LogSpudData, Error, TEXT("Cannot ReadLevelNameFromArchive, archive %s is not loading"), *Ar.GetArchiveName())
		return false;
	}

	const int64 Start = Ar.Tell();
	FSpudChunkHeader Hdr;
	Ar << Hdr;

	if (FSpudChunkHeader::EncodeMagic(SPUDDATA_LEVELDATA_MAGIC) != Hdr.Magic)
	{
		UE_LOG(LogSpudData, Error, TEXT("Cannot ReadLevelNameFromArchive from %s, next chunk is not a level"), *Ar.GetArchiveName())
		if (bReturnToStart)
			Ar.Seek(Start);
		return false;
	}

	OutDataSize = Hdr.Length;
	Ar << OutLevelName;

	if (bReturnToStart)
		Ar.Seek(Start);

	return true;
	
}

void FSpudLevelData::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	FScopeLock Lock(&Mutex);
	
	// Separate loading process since it's easier to deal with chunk robustness and versions
	if (ChunkStart(Ar))
	{
		Ar << Name;

		const uint32 MetadataID = FSpudChunkHeader::EncodeMagic(SPUDDATA_METADATA_MAGIC);
		const uint32 LevelActorsID = FSpudChunkHeader::EncodeMagic(SPUDDATA_LEVELACTORLIST_MAGIC);
		const uint32 SpawnedActorsID = FSpudChunkHeader::EncodeMagic(SPUDDATA_SPAWNEDACTORLIST_MAGIC);
		const uint32 DestroyedActorsID = FSpudChunkHeader::EncodeMagic(SPUDDATA_DESTROYEDACTORLIST_MAGIC);
		FSpudChunkHeader Hdr;
		while (IsStillInChunk(Ar))
		{
			Ar.PreviewNextChunk(Hdr, true);
			if (Hdr.Magic == MetadataID)
				Metadata.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == LevelActorsID)
				LevelActors.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == SpawnedActorsID)
				SpawnedActors.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == DestroyedActorsID)
				DestroyedActors.ReadFromArchive(Ar, StoredSystemVersion);
			else
				Ar.SkipNextChunk();
		}

		Status = LDS_Loaded;
		
		ChunkEnd(Ar);
	}
}

void FSpudLevelData::PreStoreWorld()
{
	FScopeLock Lock(&Mutex);

	// We do NOT empty the destroyed actors list because those are populated as things are removed
	// Hence why NOT calling Reset()
	Metadata.Reset();
	LevelActors.Reset();
	SpawnedActors.Reset();
}

void FSpudLevelData::Reset()
{
	FScopeLock Lock(&Mutex);
	Name = "";
	Metadata.Reset();
	LevelActors.Reset();
	SpawnedActors.Reset();
	DestroyedActors.Reset();
	Status = LDS_Unloaded;
}
bool FSpudLevelData::IsLoaded()
{
	FScopeLock Lock(&Mutex);
	return Status == LDS_Loaded;
}

void FSpudLevelData::ReleaseMemory()
{
	FScopeLock Lock(&Mutex);
	Metadata.Reset();
	LevelActors.Reset();
	SpawnedActors.Reset();
	DestroyedActors.Reset();
	Status = LDS_Unloaded;
}


//------------------------------------------------------------------------------

void FSpudGlobalData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << CurrentLevel;
		Metadata.WriteToArchive(Ar);
		Objects.WriteToArchive(Ar);
		ChunkEnd(Ar);
	}
}

void FSpudGlobalData::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	// Separate loading process since it's easier to deal with chunk robustness and versions
	if (ChunkStart(Ar))
	{
		Ar << CurrentLevel;

		const uint32 MetadataID = FSpudChunkHeader::EncodeMagic(SPUDDATA_METADATA_MAGIC);
		const uint32 ObjectsID = FSpudChunkHeader::EncodeMagic(SPUDDATA_GLOBALOBJECTLIST_MAGIC);
		FSpudChunkHeader Hdr;
		while (IsStillInChunk(Ar))
		{
			Ar.PreviewNextChunk(Hdr, true);
			if (Hdr.Magic == MetadataID)
				Metadata.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == ObjectsID)
				Objects.ReadFromArchive(Ar, StoredSystemVersion);
			else
				Ar.SkipNextChunk();
		}

		ChunkEnd(Ar);
	}	
}

void FSpudGlobalData::Reset()
{
	CurrentLevel = "";
	Metadata.Reset();
	Objects.Empty();
}

//------------------------------------------------------------------------------

void FSpudSaveInfo::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << SystemVersion;
		Ar << Title;
		FString TimestampStr = Timestamp.ToIso8601(); 
		Ar << TimestampStr;

		// This won't write anything if there isn't any screenshot data
		Screenshot.WriteToArchive(Ar);
		// Ditto, if no custom info this won't do anything
		CustomInfo.WriteToArchive(Ar);
	
		ChunkEnd(Ar);
	}
}

void FSpudSaveInfo::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		Ar << SystemVersion;
		Ar << Title;
		FString TimestampStr; 
		Ar << TimestampStr;
		FDateTime::ParseIso8601(*TimestampStr, Timestamp);

		const uint32 ScreenshotID = FSpudChunkHeader::EncodeMagic(SPUDDATA_SCREENSHOT_MAGIC);
		const uint32 CustomInfoID = FSpudChunkHeader::EncodeMagic(SPUDDATA_CUSTOMINFO_MAGIC);
		FSpudChunkHeader Hdr;
		while (IsStillInChunk(Ar))
		{
			Ar.PreviewNextChunk(Hdr, true);
			if (Hdr.Magic == ScreenshotID)
				Screenshot.ReadFromArchive(Ar, StoredSystemVersion);
			else if (Hdr.Magic == CustomInfoID)
				CustomInfo.ReadFromArchive(Ar, StoredSystemVersion);
			else
				Ar.SkipNextChunk();
		}		
		ChunkEnd(Ar);
	}
}

void FSpudSaveInfo::Reset()
{
	Title = FText();
	Screenshot.ImageData.Empty();
	CustomInfo.Reset();
}

//------------------------------------------------------------------------------
void FSpudScreenshot::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	// Don't write anything if no screenshot data
	if (ImageData.Num() > 0)
	{
		if (ChunkStart(Ar))
		{
			// Don't use << operator, just write the PNG data directly
			// Chunk header already tells us how big it is since it's the only content
			Ar.Serialize(ImageData.GetData(), ImageData.Num());
			
			ChunkEnd(Ar);
		}
	}
}

void FSpudScreenshot::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		// This chunk ONLY contains PNG data so data size from header is this
		ImageData.SetNum(ChunkHeader.Length);
		Ar.Serialize(ImageData.GetData(), ChunkHeader.Length);
		ChunkEnd(Ar);
	}
}
//------------------------------------------------------------------------------

void FSpudSaveCustomInfo::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	// Don't write the chunk at all if no data
	if (PropertyData.Num() == 0)
		return;
	
	if (ChunkStart(Ar))
	{
		Ar << PropertyNames;
		Ar << PropertyOffsets;
		Ar << PropertyData;
		ChunkEnd(Ar);
	}
}

void FSpudSaveCustomInfo::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	if (ChunkStart(Ar))
	{
		Ar << PropertyNames;
		Ar << PropertyOffsets;
		Ar << PropertyData;
		ChunkEnd(Ar);
	}
}


void FSpudSaveCustomInfo::Reset()
{
	PropertyNames.Empty();
	PropertyOffsets.Empty();
	PropertyData.Empty();
}

//------------------------------------------------------------------------------
void FSpudSaveData::PrepareForWrite()
{
	Info.SystemVersion = SPUD_CURRENT_SYSTEM_VERSION;
}

void FSpudSaveData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	WriteToArchive(Ar, "");
}

void FSpudSaveData::WriteToArchive(FSpudChunkedDataArchive& Ar, const FString& LevelPath)
{
	if (ChunkStart(Ar))
	{
		Info.WriteToArchive(Ar);	
		GlobalData.WriteToArchive(Ar);

		// Manually write the level data because its source could be memory, or piped in from files
		FSpudAdhocWrapperChunk LevelDataMapChunk(SPUDDATA_LEVELDATAMAP_MAGIC);
		if (LevelDataMapChunk.ChunkStart(Ar))
		{
			FScopeLock MapLock(&LevelDataMapMutex);
			for (auto&& KV : LevelDataMap)
			{
				auto& LevelData = KV.Value;
				// Lock outer so the status check write/copy are all locked together
				// FCriticalSection is recursive (already locked by same thread is fine)
				FScopeLock LevelLock(&LevelData->Mutex);
				
				// For level data that's not loaded, we pipe data directly from the serialized file into
				switch (LevelData->Status)
				{
				default:
				case LDS_BackgroundWriteAndUnload: // while awauting background write, data is still in memory so same as loaded (locked by mutex)
				case LDS_Loaded:
					// In memory, just write
					LevelData->WriteToArchive(Ar);
					break;
				case LDS_Unloaded:
					// This level data is not in memory. We want to pipe level data directly from the level file into
					// the combined archive so it doesn't have to go through memory
					IFileManager& FileMgr = IFileManager::Get();
					auto InLevelArchive = TUniquePtr<FArchive>(FileMgr.CreateFileReader(*GetLevelDataPath(LevelPath, LevelData->Name)));

					if (!InLevelArchive)
					{
						UE_LOG(LogSpudData, Error, TEXT("Level %s is recorded as being present but unloaded, but level data is not in file cache. "
						"This level will be missing from the save"), *LevelData->Name);
					}
					else
					{
						SpudCopyArchiveData(*InLevelArchive.Get(), Ar, InLevelArchive->TotalSize());
						InLevelArchive->Close();
					}
					break;
				}
			}
			// Finish the level container
			LevelDataMapChunk.ChunkEnd(Ar);
		}

		ChunkEnd(Ar);
	}
	
}

void FSpudSaveData::ReadFromArchive(FSpudChunkedDataArchive& Ar, bool bLoadAllLevels, const FString& LevelPath)
{
	if (ChunkStart(Ar))
	{
		FSpudChunkHeader Hdr;

		// first chunk MUST be info chunk, with no variables beforehand
		// Never change this unless you also change ReadSaveInfoFromArchive
		const uint32 InfoID = FSpudChunkHeader::EncodeMagic(SPUDDATA_SAVEINFO_MAGIC);
		Ar.PreviewNextChunk(Hdr);
		if (Hdr.Magic != InfoID)
		{
			UE_LOG(LogSpudData, Error, TEXT("Save data is corrupt, first chunk MUST be the INFO chunk."));
			return;
		}

		Info.ReadFromArchive(Ar, 0);

		bool bOrigLoadAllLevels = bLoadAllLevels;
		bool bIsUpgrading = false;
		if (Ar.IsLoading() && Info.SystemVersion != SPUD_CURRENT_SYSTEM_VERSION)
		{
			// System version upgrade, we need to load all levels to fix, then page out
			UE_LOG(LogSpudData, Log, TEXT("Save file %s is an old system version, automatically upgrading..."), *Ar.GetArchiveName())
			bLoadAllLevels = true;
			bIsUpgrading = true;
		}
		const uint32 GlobalDataID = FSpudChunkHeader::EncodeMagic(SPUDDATA_GLOBALDATA_MAGIC);
		const uint32 LevelDataMapID = FSpudChunkHeader::EncodeMagic(SPUDDATA_LEVELDATAMAP_MAGIC);
		while (IsStillInChunk(Ar))
		{
			Ar.PreviewNextChunk(Hdr, true);
			if (Hdr.Magic == GlobalDataID)
				GlobalData.ReadFromArchive(Ar, Info.SystemVersion);
			else if (Hdr.Magic == LevelDataMapID)
			{
				// Read levels using adhoc wrapper so we can choose what to do for each
				FSpudAdhocWrapperChunk LevelDataMapChunk(SPUDDATA_LEVELDATAMAP_MAGIC);
				if (LevelDataMapChunk.ChunkStart(Ar))
				{
					{
						FScopeLock MapMutex(&LevelDataMapMutex);					
						LevelDataMap.Empty();
					}

					// Detect chunks & only load compatible
					const uint32 LevelMagicID = FSpudChunkHeader::EncodeMagic(SPUDDATA_LEVELDATA_MAGIC);
					while (IsStillInChunk(Ar))
					{
						if (Ar.NextChunkIs(LevelMagicID))
						{
							if (bLoadAllLevels)
							{
								TLevelDataPtr LvlData(new FSpudLevelData());
								LvlData->ReadFromArchive(Ar, Info.SystemVersion);
								{
									FScopeLock MapMutex(&LevelDataMapMutex);					
									LevelDataMap.Add(LvlData->Key(), LvlData);
								}
							}
							else
							{
								// Pipe data for this level into its own file rather than load it
								// We need to know the level name though
								FString LevelName;
								int64 LevelDataSize;
								if (FSpudLevelData::ReadLevelInfoFromArchive(Ar, true, LevelName, LevelDataSize))
								{
									IFileManager& FileMgr = IFileManager::Get();
									auto OutLevelArchive = TUniquePtr<FArchive>(FileMgr.CreateFileWriter(*GetLevelDataPath(LevelPath, LevelName)));

									int64 TotalSize = LevelDataSize + FSpudChunkHeader::GetHeaderSize();
									SpudCopyArchiveData(Ar, *OutLevelArchive.Get(), TotalSize);
									OutLevelArchive->Close();
									
                                    TLevelDataPtr LvlData(new FSpudLevelData());
									LvlData->Name = LevelName;
									LvlData->Status = LDS_Unloaded;
									{
										FScopeLock MapMutex(&LevelDataMapMutex);					
										LevelDataMap.Add(LvlData->Key(), LvlData);
									}
								}
							}
						}
						else
						{
							Ar.SkipNextChunk();
						}
					}
					
					LevelDataMapChunk.ChunkEnd(Ar);
				}
				
			}
			else
				Ar.SkipNextChunk();
		}

		if (bIsUpgrading)
			UE_LOG(LogSpudData, Log, TEXT("Save file %s upgrade complete. Not changed on disk, will be saved in new format next time."), *Ar.GetArchiveName())

		if (bLoadAllLevels && !bOrigLoadAllLevels)
		{
			// We forced a load of all levels even though the caller didn't want it (perhaps because of upgrade)
			// So now unload them
			WriteAndReleaseAllLevelData(LevelPath);
		}

		ChunkEnd(Ar);
	}

}

void FSpudSaveData::ReadFromArchive(FSpudChunkedDataArchive& Ar, uint32 StoredSystemVersion)
{
	ReadFromArchive(Ar, true, "");
}


void FSpudSaveData::Reset()
{
	Info.Reset();
	GlobalData.Reset();
	{
		FScopeLock MapMutex(&LevelDataMapMutex);
		LevelDataMap.Empty();
	}
}

FSpudSaveData::TLevelDataPtr FSpudSaveData::CreateLevelData(const FString& LevelName)
{
	TLevelDataPtr NewLevelData(new FSpudLevelData());
	NewLevelData->Name = LevelName;
	NewLevelData->Status = LDS_Loaded; // assume loaded if we're creating

	{
		FScopeLock MapMutex(&LevelDataMapMutex);
		LevelDataMap.Add(LevelName, NewLevelData);
	}
	
	return NewLevelData;
}

void FSpudSaveData::DeleteAllLevelDataFiles(const FString& LevelPath)
{
	IFileManager& FM = IFileManager::Get();
	
	TArray<FString> LevelFiles;
	FM.FindFiles(LevelFiles, *LevelPath, TEXT(".lvl"));

	for (auto && File : LevelFiles)
	{
		// We want to parse just the very first part of the file, not all of it
		FString AbsoluteFilename = FPaths::Combine(LevelPath, File);
		FM.Delete(*AbsoluteFilename);
	}

}

FString FSpudSaveData::GetLevelDataPath(const FString& LevelPath, const FString& LevelName)
{
	return FString::Printf(TEXT("%s%s.lvl"), *LevelPath, *LevelName);		
}

void FSpudSaveData::WriteLevelData(FSpudLevelData& LevelData, const FString& LevelName, const FString& LevelPath)
{
	IFileManager& FileMgr = IFileManager::Get();
	const FString Filename = GetLevelDataPath(LevelPath, LevelName);
	const auto Archive = TUniquePtr<FArchive>(FileMgr.CreateFileWriter(*Filename));

	if (Archive)
	{
		FSpudChunkedDataArchive ChunkedAr(*Archive);
		LevelData.WriteToArchive(ChunkedAr);
		// Always explicitly close to catch errors from flush/close
		ChunkedAr.Close();

		if (ChunkedAr.IsError() || ChunkedAr.IsCriticalError())
		{
			UE_LOG(LogSpudData, Error, TEXT("Error while writing level data to %s"), *Filename);
		}
	}
	else
	{
		UE_LOG(LogSpudData, Error, TEXT("Error opening level data file for writing: %s"), *Filename);
	}
	
}

bool FSpudSaveData::ReadSaveInfoFromArchive(FSpudChunkedDataArchive& Ar, FSpudSaveInfo& OutInfo)
{
	// Read manually, no stateful ChunkStart/End
	FSpudChunkHeader Hdr;
	Ar.PreviewNextChunk(Hdr, false); // do not seek back after preview, we want to read info

	if (Hdr.Magic != FSpudChunkHeader::EncodeMagic(SPUDDATA_SAVEGAME_MAGIC))
	{
		UE_LOG(LogSpudData, Error, TEXT("Cannot get info for save game, file is not a save game"))
		return false;
	}

	if (!Ar.NextChunkIs(SPUDDATA_SAVEINFO_MAGIC))
	{
		UE_LOG(LogSpudData, Error, TEXT("Cannot get info for save game, INFO chunk isn't present at start"))
		return false;		
	}
	OutInfo.ReadFromArchive(Ar, 0);

	return true;
	
}


FSpudSaveData::TLevelDataPtr FSpudSaveData::GetLevelData(const FString& LevelName, bool bLoadIfNeeded, const FString& LevelPath)
{
	TLevelDataPtr Ret;
	{
		// Only lock the map while looking up
		// We get a shared pointer back (threadsafe) and lock its own mutex before changing the instance state
		FScopeLock MapMutex(&LevelDataMapMutex);
		const auto Found = LevelDataMap.Find(LevelName);
		if (Found)
			Ret = *Found;
	}
	if (Ret.IsValid() && bLoadIfNeeded)
	{
		FScopeLock LevelLock(&Ret->Mutex);
		switch (Ret->Status)
		{
		case LDS_Unloaded:
			{
				// Load individual level file back into memory
				IFileManager& FileMgr = IFileManager::Get();
				const auto Filename = GetLevelDataPath(LevelPath, LevelName);
				const auto Archive = TUniquePtr<FArchive>(FileMgr.CreateFileReader(*Filename));

				if (Archive)
				{
					FSpudChunkedDataArchive ChunkedAr(*Archive);

					// We have to assume that leveldata has been upgraded at load time if system version was incorrect
					Ret->ReadFromArchive(ChunkedAr, SPUD_CURRENT_SYSTEM_VERSION);
					ChunkedAr.Close();

					if (ChunkedAr.IsError() || ChunkedAr.IsCriticalError())
					{
						UE_LOG(LogSpudData, Error, TEXT("Error while loading active game level file from %s"), *Filename);
					}
				}
				else
				{
					UE_LOG(LogSpudData, Error, TEXT("Error opening active game level state file %s"), *Filename);
				}
				break;
			}
		case LDS_BackgroundWriteAndUnload:
			// Loading in this state is just flipping back to loaded, because all the state is still in memory
			// We're just waiting for it to be written out and released
			// By changing the status back to loaded, the background unload task will skip the unload
			Ret->Status = LDS_Loaded;
			break;
		default:
		case LDS_Loaded:
			break;
		}
	}

	return Ret;
}


void FSpudSaveData::WriteAndReleaseAllLevelData(const FString& LevelPath)
{
	FScopeLock MapLock(&LevelDataMapMutex);
	for (auto && Pair : LevelDataMap)
	{
		WriteAndReleaseLevelData(Pair.Key, LevelPath, true);
	}
}

bool FSpudSaveData::WriteAndReleaseLevelData(const FString& LevelName, const FString& LevelPath, bool bBlocking)
{
	auto LevelData = GetLevelData(LevelName, false, "");
	if (LevelData.IsValid())
	{
		FScopeLock LevelLock(&LevelData->Mutex);
		if (LevelData->Status == LDS_Loaded ||
			// If we've queued a background write & unload but this is now requesting a blocking write, we
			// should upgrade it and do it NOW. When the status is changed to LDS_Unloaded the background worker will ignore it
			LevelData->Status == LDS_BackgroundWriteAndUnload && bBlocking)
		{
			if (bBlocking)
			{
				WriteLevelData(*LevelData, LevelName, LevelPath);
				LevelData->ReleaseMemory();
			}
			else
			{
				// My first thought here was to Swap() the loaded memory and fire that off into the background thread
				// thus disconnecting it and not having to worry about locking afterwards
				// But the problem was if the write was queued and then another request for the level data came in
				// before it was written to disk, the state could be entirely lost, since the only record of it was
				// in a disconnected background thread. So instead, I've chosen to keep the data in the struct and
				// just mark it as pending write and unload. That way if another request comes in before the write happens,
				// the data can just be resurrected in-place.
				// The downside is that this requires some locking so although primary I/O stalling is removed, overlapping
				// write and a request for another level can potentially have locking contention which could cause its
				// own stalls. However this is still much less likely. If it becomes an issue then we may need to use
				// a more granular approach to locking (separating status and still copying data perhaps) but that's more
				// complex & prone to slip-ups, so keeping it simpler for now.
				
				LevelData->Status = LDS_BackgroundWriteAndUnload;

				// Write this level data to disk in a background thread
				// Only pass the level name and not the pointer, this is then safe from the list being cleared
				AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, LevelName, LevelPath]()
                {
					auto LevelData = GetLevelData(LevelName, false, "");
                    if (LevelData.IsValid())
                    {
	                    // Re-acquire lock and check still unloading
                        FScopeLock LevelLock(&LevelData->Mutex);
                        if (LevelData->Status == LDS_BackgroundWriteAndUnload)
                        {
                            WriteLevelData(*LevelData, LevelName, LevelPath);
                            LevelData->ReleaseMemory();
                        }
                    }
                });
			}
		}
	}
	return true;
}

void FSpudSaveData::DeleteLevelData(const FString& LevelName, const FString& LevelPath)
{
	{
		FScopeLock MapMutex(&LevelDataMapMutex);
		LevelDataMap.Remove(LevelName);
	}

	IFileManager& FileMgr = IFileManager::Get();
	const FString Filename = GetLevelDataPath(LevelPath, LevelName);
	FileMgr.Delete(*Filename, false, true, true);
	
}

//------------------------------------------------------------------------------
int64 SpudCopyArchiveData(FArchive& InArchive, FArchive& OutArchive, int64 Length)
{
	// file read / write archives have their own buffers too but I can't assume
	constexpr int BufferLen = 4096;
	uint8 TempBuffer[BufferLen];

	int64 BytesCopied = 0;
	if (InArchive.IsLoading() && OutArchive.IsSaving())
	{
		while (BytesCopied < Length)
		{
			int64 BytesToRequest = std::min(Length - BytesCopied, static_cast<int64>(BufferLen));
			InArchive.Serialize(TempBuffer, BytesToRequest);
			if (InArchive.IsError())
			{
				UE_LOG(LogSpudData, Error, TEXT("Error during read while copying archive data from %s to %s"), *InArchive.GetArchiveName(), *OutArchive.GetArchiveName());
				break; // actual error will have been reported
			}

			OutArchive.Serialize(TempBuffer, BytesToRequest);
			if (OutArchive.IsError())
			{
				UE_LOG(LogSpudData, Error, TEXT("Error during write while copying archive data from %s to %s"), *InArchive.GetArchiveName(), *OutArchive.GetArchiveName());
				break; // actual error will have been reported
			}

			BytesCopied += BytesToRequest;
			
		}
	}
	else
	{
		UE_LOG(LogSpudData, Error, TEXT("Cannot copy archive data from %s to %s, mismatched loading/saving status"), *InArchive.GetArchiveName(), *OutArchive.GetArchiveName());
	}
	return BytesCopied;
}