#include "../Public/SpudMemoryReaderWriter.h"
#include "UObject/Object.h"

FArchive& FSpudMemoryWriter::operator<<(UObject*& Obj)
{
	// save out the fully qualified object name
	FString SavedString(Obj->GetPathName());
	*this << SavedString;
	return *this;
	
}

FArchive& FSpudMemoryReader::operator<<(UObject*& Obj)
{
	// load the path name to the object
	FString LoadedString;
	*this << LoadedString;
	// look up the object by fully qualified pathname
	Obj = FindObject<UObject>(nullptr, *LoadedString, false);
	// If we couldn't find it, and we want to load it, do that
	if(!Obj)
	{
		Obj = LoadObject<UObject>(nullptr, *LoadedString);
	}
	return *this;
}
