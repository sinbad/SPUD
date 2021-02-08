#include "SpudData.h"

#include <algorithm>

#include "SpudPropertyUtil.h"

PRAGMA_DISABLE_OPTIMIZATION

DEFINE_LOG_CATEGORY(LogSpudData)

#define SPUD_SAVEGAME_CURRENT_VERSION 1

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

void FSpudLevelData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
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
			for (auto&& KV : LevelDataMap)
			{
				auto& LevelData = KV.Value;
				// Lock outer so the status check write/copy are all locked together
				// FCriticalSection is recursive (already locked by same thread is fine)
				FScopeLock LevelLock(&LevelData.Mutex);
				
				// For level data that's not loaded, we pipe data directly from the serialized file into
				switch (LevelData.Status)
				{
				default:
				case LDS_BackgroundWriteAndUnload: // while awauting background write, data is still in memory so same as loaded (locked by mutex)
				case LDS_Loaded:
					// In memory, just write
					LevelData.WriteToArchive(Ar);
					break;
				case LDS_Unloaded:
					// This level data is not in memory. We want to pipe level data directly from the level file into
					// the combined archive so it doesn't have to go through memory
					IFileManager& FileMgr = IFileManager::Get();
					auto InLevelArchive = TUniquePtr<FArchive>(FileMgr.CreateFileReader(*GetLevelDataPath(LevelPath, LevelData.Name)));

					SpudCopyArchiveData(*InLevelArchive.Get(), Ar, InLevelArchive->TotalSize());
					InLevelArchive->Close();
					break;
				}
			}
			// Finish the level container
			LevelDataMapChunk.ChunkEnd(Ar);
		}

		// TODO: screenshot / customdata chunks

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
				// Read levels using adhoc wrapper so we can choose what to do for each
				FSpudAdhocWrapperChunk LevelDataMapChunk(SPUDDATA_LEVELDATAMAP_MAGIC);
				if (LevelDataMapChunk.ChunkStart(Ar))
				{
					FScopeLock MapMutex(&LevelDataMapMutex);					
					LevelDataMap.Empty();

					// Detect chunks & only load compatible
					const uint32 LevelMagicID = FSpudChunkHeader::EncodeMagic(SPUDDATA_LEVELDATA_MAGIC);
					while (IsStillInChunk(Ar))
					{
						if (Ar.NextChunkIs(LevelMagicID))
						{
							if (bLoadAllLevels)
							{
								FSpudLevelData LvlData;
								LvlData.ReadFromArchive(Ar);
								LevelDataMap.Add(LvlData.Key(), LvlData);						
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
									
                                    FSpudLevelData LvlData;
									LvlData.Name = LevelName;
									LvlData.Name = LevelName;
									LvlData.Status = LDS_Unloaded;
									LevelDataMap.Add(LvlData.Key(), LvlData);
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

		ChunkEnd(Ar);
	}
}

void FSpudSaveData::ReadFromArchive(FSpudChunkedDataArchive& Ar)
{
	ReadFromArchive(Ar, true, "");
}


void FSpudSaveData::Reset()
{
	GlobalData.Reset();
	FScopeLock MapMutex(&LevelDataMapMutex);
	LevelDataMap.Empty();
}

FSpudLevelData* FSpudSaveData::CreateLevelData(const FString& LevelName)
{
	FScopeLock MapMutex(&LevelDataMapMutex);
	auto Ret = &LevelDataMap.Add(LevelName);
	Ret->Name = LevelName;
	Ret->Status = LDS_Loaded; // assume loaded if we're creating
	return Ret;
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
	OutInfo.ReadFromArchive(Ar);

	return true;
	
}


FSpudLevelData* FSpudSaveData::GetLevelData(const FString& LevelName, bool bLoadIfNeeded, const FString& LevelPath)
{
	FScopeLock MapMutex(&LevelDataMapMutex);
	auto Ret = LevelDataMap.Find(LevelName);
	if (Ret && bLoadIfNeeded)
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

					Ret->ReadFromArchive(ChunkedAr);
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

bool FSpudSaveData::WriteAndReleaseLevelData(const FString& LevelName, const FString& LevelPath, bool bBlocking)
{
	FScopeLock MapMutex(&LevelDataMapMutex);
	auto LevelData = LevelDataMap.Find(LevelName);
	if (LevelData)
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
				LevelData->Status = LDS_BackgroundWriteAndUnload;

				// Write this level data to disk in a background thread
				// Only pass the level name and not the pointer, this is then safe from the list being cleared
				AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, LevelName, LevelPath]()
                {
					FScopeLock LevelMapLock(&LevelDataMapMutex);
					auto LevelData = LevelDataMap.Find(LevelName);
                    if (LevelData)
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
	FScopeLock MapMutex(&LevelDataMapMutex);
	LevelDataMap.Remove(LevelName);

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

PRAGMA_ENABLE_OPTIMIZATION
