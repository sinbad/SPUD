// Fill out your copyright notice in the Description page of Project Settings.


#include "TestSaveObject.h"

const FString UTestSaveObjectCustomData::TestChunkID1("chk1");
const FString UTestSaveObjectCustomData::TestChunkID2("chk2");

void UTestSaveObjectCustomData::SpudStoreCustomData_Implementation(const USpudState* State,
	USpudStateCustomData* CustomData)
{
	// store as a custom chunk, nested
	CustomData->BeginWriteChunk(TestChunkID1);
	CustomData->WriteByte((uint8)bSomeBoolean);

	// Nested
	CustomData->BeginWriteChunk(TestChunkID2);
	CustomData->WriteInt(SomeInteger);
	CustomData->WriteString(SomeString);
	CustomData->EndWriteChunk(TestChunkID2);


	CustomData->WriteFloat(SomeFloat);
	CustomData->EndWriteChunk(TestChunkID1);
	
}

void UTestSaveObjectCustomData::SpudRestoreCustomData_Implementation(USpudState* State,
	USpudStateCustomData* CustomData)
{
	// This method includes a lot of double checking, you'd normally just mirror the store in read form
	// However for the test we're going to go back & forth, peeking and skipping as well to make sure that works.
	const uint64 OrigPos = CustomData->GetUnderlyingArchive()->Tell();
	uint64 PostNestedPos = 0;

	Peek1Succeeded = false;
	Peek1IDOK = false;
	Peek2Succeeded = false;
	Peek2IDOK = false;
	Skip1Succeeded = false;
	Skip1PosOK = false;
	Skip2Succeeded = false;
	Skip2PosOK = false;
	
	// Test peeking as well while we're doing it
	FString PeekChunkID;
	Peek1Succeeded = CustomData->PeekChunk(PeekChunkID);
	Peek1IDOK = PeekChunkID == TestChunkID1;
	
	if (CustomData->BeginReadChunk(TestChunkID1))
	{
		uint8 Byte;
		CustomData->ReadByte(Byte);
		bSomeBoolean = (bool)Byte;

		// Nested
		Peek2Succeeded = CustomData->PeekChunk(PeekChunkID);
		Peek2IDOK = PeekChunkID == TestChunkID2;

		if (CustomData->BeginReadChunk(TestChunkID2))
		{
			CustomData->ReadInt(SomeInteger);
			CustomData->ReadString(SomeString);
			CustomData->EndReadChunk(TestChunkID2);
			PostNestedPos = CustomData->GetUnderlyingArchive()->Tell();
		}

		CustomData->ReadFloat(SomeFloat);
		CustomData->EndReadChunk(TestChunkID1);
	}

	// Record end pos for more testing
	const uint64 EndPos = CustomData->GetUnderlyingArchive()->Tell();

	// Now test skipping
	CustomData->GetUnderlyingArchive()->Seek(OrigPos);

	// Should be able to skip entire 1st chunk & skip everything
	Skip1Succeeded = CustomData->SkipChunk(TestChunkID1);
	Skip1PosOK = CustomData->GetUnderlyingArchive()->Tell() == EndPos;
	
	// Back to start so we can test skiping nested only
	CustomData->GetUnderlyingArchive()->Seek(OrigPos);

	// Read outer & discard
	if (CustomData->BeginReadChunk(TestChunkID1))
	{
		uint8 Byte;
		CustomData->ReadByte(Byte);

		// Skip nested
		Skip2Succeeded = CustomData->SkipChunk(TestChunkID2);
		Skip2PosOK = CustomData->GetUnderlyingArchive()->Tell() == PostNestedPos;
	}

	// Jump to end juat for consistency
	CustomData->GetUnderlyingArchive()->Seek(EndPos);

}
