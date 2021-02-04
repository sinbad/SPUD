#include "SpudData.h"
#include "SpudPropertyUtil.h"

DEFINE_LOG_CATEGORY(LogSpudData)

#define SPUD_SAVEGAME_CURRENT_VERSION 1

//------------------------------------------------------------------------------

bool FSpudChunkedDataArchive::PreviewNextChunk(FSpudChunkHeader& OutHeader, bool SeekBackToHeader)
{
	if (!IsLoading())
		return false;

	const int64 CurrPos = Tell();

	if ((CurrPos + FSpudChunkHeader::GetDataSize()) > TotalSize())
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

void FSpudClassDef::ReadFromArchive(FSpudChunkedDataArchive& Ar)
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
	Ar << PropertyOffsets;
	FSpudDataHolder::WriteToArchive(Ar);
}

void FSpudPropertyData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
{
	PropertyOffsets.Empty();
	Ar << PropertyOffsets;
	FSpudDataHolder::ReadFromArchive(Ar);
}

void FSpudPropertyData::Reset()
{
	FSpudDataHolder::Reset();
	PropertyOffsets.Empty();
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
void FSpudDataHolder::ReadFromArchive(FSpudChunkedDataArchive& Ar)
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

void FSpudDestroyedLevelActor::ReadFromArchive(FSpudChunkedDataArchive& Ar)
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

void FSpudNamedObjectData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << Name;
		CoreData.ReadFromArchive(Ar);
		Properties.ReadFromArchive(Ar);
		CustomData.ReadFromArchive(Ar);
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

void FSpudSpawnedActorData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << ClassID;
		Ar << Guid;
		CoreData.ReadFromArchive(Ar);
		Properties.ReadFromArchive(Ar);
		CustomData.ReadFromArchive(Ar);
		ChunkEnd(Ar);
	}
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
		ClassNameIndex.WriteToArchive(Ar);
		ClassDefinitions.WriteToArchive(Ar);
		PropertyNameIndex.WriteToArchive(Ar);
		ChunkEnd(Ar);
	}
}

void FSpudClassMetadata::ReadFromArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		const uint32 ClassNameIndexID = FSpudChunkHeader::EncodeMagic(SPUDDATA_CLASSNAMEINDEX_MAGIC);
		const uint32 ClassDefListID = FSpudChunkHeader::EncodeMagic(SPUDDATA_CLASSDEFINITIONLIST_MAGIC);
		const uint32 PropertyNameIndexID = FSpudChunkHeader::EncodeMagic(SPUDDATA_PROPERTYNAMEINDEX_MAGIC);
		FSpudChunkHeader Hdr;
		while (IsStillInChunk(Ar))
		{
			Ar.PreviewNextChunk(Hdr, true);
			if (Hdr.Magic == ClassNameIndexID)
				ClassNameIndex.ReadFromArchive(Ar);
			else if (Hdr.Magic == ClassDefListID)
				ClassDefinitions.ReadFromArchive(Ar);
			else if (Hdr.Magic == PropertyNameIndexID)
				PropertyNameIndex.ReadFromArchive(Ar);
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

//------------------------------------------------------------------------------
void FSpudLevelData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
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

void FSpudLevelData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
{
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
				Metadata.ReadFromArchive(Ar);
			else if (Hdr.Magic == LevelActorsID)
				LevelActors.ReadFromArchive(Ar);
			else if (Hdr.Magic == SpawnedActorsID)
				SpawnedActors.ReadFromArchive(Ar);
			else if (Hdr.Magic == DestroyedActorsID)
				DestroyedActors.ReadFromArchive(Ar);
			else
				Ar.SkipNextChunk();
		}

		ChunkEnd(Ar);
	}
}

void FSpudLevelData::PreStoreWorld()
{
	// We do NOT empty the destroyed actors list because those are populated as things are removed
	// Hence why NOT calling Reset()
	Metadata.Reset();
	LevelActors.Reset();
	SpawnedActors.Reset();
}

void FSpudLevelData::Reset()
{
	Metadata.Reset();
	LevelActors.Reset();
	SpawnedActors.Reset();
	DestroyedActors.Reset();
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

void FSpudGlobalData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
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
				Metadata.ReadFromArchive(Ar);
			else if (Hdr.Magic == ObjectsID)
				Objects.ReadFromArchive(Ar);
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
		Ar << Version;
		Ar << Title;
		FString TimestampStr = Timestamp.ToIso8601(); 
		Ar << TimestampStr;

		// TODO write chunks for screenshot and custom user data
		ChunkEnd(Ar);
	}
}

void FSpudSaveInfo::ReadFromArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Ar << Version;
		Ar << Title;
		FString TimestampStr; 
		Ar << TimestampStr;
		FDateTime::ParseIso8601(*TimestampStr, Timestamp);

		// TODO write chunks for screenshot and custom user data
		ChunkEnd(Ar);
	}
}

//------------------------------------------------------------------------------
void FSpudSaveData::PrepareForWrite(const FText& Title)
{
	Info.Title = Title;
	Info.Version = SPUD_SAVEGAME_CURRENT_VERSION;
	Info.Timestamp = FDateTime::Now();
}

void FSpudSaveData::WriteToArchive(FSpudChunkedDataArchive& Ar)
{
	if (ChunkStart(Ar))
	{
		Info.WriteToArchive(Ar);	
		GlobalData.WriteToArchive(Ar);
		// Important: in this case we're writing a combined save game, so all levels are present
		LevelDataMap.WriteToArchive(Ar);

		// TODO: read screenshot / customdata chunks

		ChunkEnd(Ar);
	}
}

void FSpudSaveData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
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

		Info.ReadFromArchive(Ar);

		if (Ar.IsLoading() && Info.Version != SPUD_SAVEGAME_CURRENT_VERSION)
		{
			// TODO: Deal with any version incompatibilities here
		}
		const uint32 GlobalDataID = FSpudChunkHeader::EncodeMagic(SPUDDATA_GLOBALDATA_MAGIC);
		const uint32 LevelDataMapID = FSpudChunkHeader::EncodeMagic(SPUDDATA_LEVELDATAMAP_MAGIC);
		while (IsStillInChunk(Ar))
		{
			Ar.PreviewNextChunk(Hdr, true);
			if (Hdr.Magic == GlobalDataID)
				GlobalData.ReadFromArchive(Ar);
			else if (Hdr.Magic == LevelDataMapID)
			{
				// Important: in this case we're reading from a combined save game, so all levels are present
				LevelDataMap.ReadFromArchive(Ar);
			}
			else
				Ar.SkipNextChunk();
		}

		ChunkEnd(Ar);
	}
}

void FSpudSaveData::Reset()
{
	GlobalData.Reset();
	LevelDataMap.Empty();
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
	OutInfo.ReadFromArchive(Ar);

	return true;
	
}
