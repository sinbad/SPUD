﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ISpudObject.h"
#if ENGINE_MAJOR_VERSION==5&&ENGINE_MINOR_VERSION>=5
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif
#include "UObject/Object.h"
#include "TestSaveObject.generated.h"


UENUM(BlueprintType)
enum class ETestEnum : uint8
{
	First,
	Second,
	Third,
};

/// Simple nested UObject
UCLASS()
class SPUDTEST_API UTestNestedUObject : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	FString NestedStringVal;

	UPROPERTY(SaveGame)
	int NestedIntVal;
	
};

/// A test with all values in a struct
USTRUCT(BlueprintType)
struct FTestAllTypesStruct
{
	GENERATED_USTRUCT_BODY()

public:
	// Primitive types
	UPROPERTY(SaveGame)
	int IntVal = 0;
	UPROPERTY(SaveGame)
	uint8 UInt8Val = 0;
	UPROPERTY(SaveGame)
	uint16 UInt16Val = 0;
	UPROPERTY(SaveGame)
	uint32 UInt32Val = 0;
	UPROPERTY(SaveGame)
	uint64 UInt64Val = 0;
	UPROPERTY(SaveGame)
	int8 Int8Val = 0;
	UPROPERTY(SaveGame)
	int16 Int16Val = 0;
	UPROPERTY(SaveGame)
	int32 Int32Val = 0;
	UPROPERTY(SaveGame)
	int64 Int64Val = 0;

	UPROPERTY(SaveGame)
	float FloatVal = 0;
	UPROPERTY(SaveGame)
	double DoubleVal = 0;
	
	UPROPERTY(SaveGame)
	ETestEnum EnumVal = ETestEnum::First;

	// Built in structs
	UPROPERTY(SaveGame)
	FVector VectorVal = FVector::ZeroVector;
	UPROPERTY(SaveGame)
	FRotator RotatorVal = FRotator::ZeroRotator;
	UPROPERTY(SaveGame)
	FTransform TransformVal = FTransform::Identity;

	// Strings etc
	UPROPERTY(SaveGame)
	FName NameVal;
	UPROPERTY(SaveGame)
	FString StringVal;
	UPROPERTY(SaveGame)
	FText TextVal;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedUObject> UObjectVal = nullptr;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedUObject> TObjectPtrVal = nullptr;

	UPROPERTY(SaveGame)
	TArray< TObjectPtr<UTestNestedUObject> > TObjectPtrArray;
	
	UPROPERTY(SaveGame)
	TSubclassOf<AActor> ActorSubclass;
	
	UPROPERTY(SaveGame)
	TArray< TSubclassOf<AActor> > ActorSubclassArray;
	// sadly we can't test actor refs easily here; test example world does that though

	UPROPERTY(SaveGame)
	TMap<int, TObjectPtr<UTestNestedUObject>> UObjectMap;

	// Arrays of the above
	UPROPERTY(SaveGame)
	TArray<int> IntArray;
	UPROPERTY(SaveGame)
	TArray<uint8> UInt8Array;
	UPROPERTY(SaveGame)
	TArray<uint16> UInt16Array;
	UPROPERTY(SaveGame)
	TArray<uint32> UInt32Array;
	UPROPERTY(SaveGame)
	TArray<uint64> UInt64Array;
	UPROPERTY(SaveGame)
	TArray<int8> Int8Array;
	UPROPERTY(SaveGame)
	TArray<int16> Int16Array;
	UPROPERTY(SaveGame)
	TArray<int32> Int32Array;
	UPROPERTY(SaveGame)
	TArray<int64> Int64Array;
	UPROPERTY(SaveGame)
	TArray<float> FloatArray;
	UPROPERTY(SaveGame)
	TArray<double> DoubleArray;
	UPROPERTY(SaveGame)
	TArray<ETestEnum> EnumArray;
	UPROPERTY(SaveGame)
	TArray<FVector> VectorArray;
	UPROPERTY(SaveGame)
	TArray<FRotator> RotatorArray;
	UPROPERTY(SaveGame)
	TArray<FTransform> TransformArray;
	UPROPERTY(SaveGame)
	TArray<FName> NameArray;
	UPROPERTY(SaveGame)
	TArray<FString> StringArray;
	UPROPERTY(SaveGame)
	TArray<FText> TextArray;

};

// Test 2-level nesting
USTRUCT(BlueprintType)
struct FTestNestedStruct
{
	GENERATED_USTRUCT_BODY()

public:

	UPROPERTY(SaveGame)
	FString TestString;

	UPROPERTY(SaveGame)
	FTestAllTypesStruct Nested;
};

UCLASS()
class SPUDTEST_API UTestSaveObjectBasic : public UObject
{
	GENERATED_BODY()
public:
	// Primitive types
	UPROPERTY(SaveGame)
	int IntVal;
	UPROPERTY(SaveGame)
	uint8 UInt8Val;
	UPROPERTY(SaveGame)
	uint16 UInt16Val;
	UPROPERTY(SaveGame)
	uint32 UInt32Val;
	UPROPERTY(SaveGame)
	uint64 UInt64Val;
	UPROPERTY(SaveGame)
	int8 Int8Val;
	UPROPERTY(SaveGame)
	int16 Int16Val;
	UPROPERTY(SaveGame)
	int32 Int32Val;
	UPROPERTY(SaveGame)
	int64 Int64Val;

	UPROPERTY(SaveGame)
	float FloatVal;
	UPROPERTY(SaveGame)
	double DoubleVal;
	
	UPROPERTY(SaveGame)
	ETestEnum EnumVal;

	// Built in structs
	UPROPERTY(SaveGame)
	FVector VectorVal;
	UPROPERTY(SaveGame)
	FRotator RotatorVal;
	UPROPERTY(SaveGame)
	FTransform TransformVal;

	// Strings etc
	UPROPERTY(SaveGame)
	FName NameVal;
	UPROPERTY(SaveGame)
	FString StringVal;
	UPROPERTY(SaveGame)
	FText TextVal;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedUObject> UObjectVal;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedUObject> TObjectPtrVal = nullptr;

	UPROPERTY(SaveGame)
	TArray< TObjectPtr<UTestNestedUObject> > TObjectPtrArray;

	UPROPERTY(SaveGame)
	TSubclassOf<AActor> ActorSubclass;

	UPROPERTY(SaveGame)
	TArray< TSubclassOf<AActor> > ActorSubclassArray;

	UPROPERTY(SaveGame)
	TMap<int, TObjectPtr<UObject>> UObjectMap;
	
	// sadly we can't test actor refs easily here; test example world does that though

	// Arrays of the above
	UPROPERTY(SaveGame)
	TArray<int> IntArray;
	UPROPERTY(SaveGame)
	TArray<uint8> UInt8Array;
	UPROPERTY(SaveGame)
	TArray<uint16> UInt16Array;
	UPROPERTY(SaveGame)
	TArray<uint32> UInt32Array;
	UPROPERTY(SaveGame)
	TArray<uint64> UInt64Array;
	UPROPERTY(SaveGame)
	TArray<int8> Int8Array;
	UPROPERTY(SaveGame)
	TArray<int16> Int16Array;
	UPROPERTY(SaveGame)
	TArray<int32> Int32Array;
	UPROPERTY(SaveGame)
	TArray<int64> Int64Array;
	UPROPERTY(SaveGame)
	TArray<float> FloatArray;
	UPROPERTY(SaveGame)
	TArray<double> DoubleArray;
	UPROPERTY(SaveGame)
	TArray<ETestEnum> EnumArray;
	UPROPERTY(SaveGame)
	TArray<FVector> VectorArray;
	UPROPERTY(SaveGame)
	TArray<FRotator> RotatorArray;
	UPROPERTY(SaveGame)
	TArray<FTransform> TransformArray;
	UPROPERTY(SaveGame)
	TArray<FName> NameArray;
	UPROPERTY(SaveGame)
	TArray<FString> StringArray;
	UPROPERTY(SaveGame)
	TArray<FText> TextArray;
};


UCLASS()
class SPUDTEST_API UTestSaveObjectStructs : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	FTestAllTypesStruct SimpleStruct;	

	UPROPERTY(SaveGame)
	FTestNestedStruct NestedStruct;	

	UPROPERTY(SaveGame)
	FInstancedStruct InstancedStruct = FInstancedStruct::Make(SimpleStruct);
};

UCLASS()
class SPUDTEST_API UTestSaveObjectCustomData : public UObject, public ISpudObjectCallback
{
	GENERATED_BODY()
public:

	bool bSomeBoolean;
	int SomeInteger;
	FString SomeString;
	float SomeFloat;

	// just for test read
	bool Peek1Succeeded;
	bool Peek1IDOK;
	bool Peek2Succeeded;
	bool Peek2IDOK;
	bool Skip1Succeeded;
	bool Skip1PosOK;
	bool Skip2Succeeded;
	bool Skip2PosOK;
	
	static const FString TestChunkID1;
	static const FString TestChunkID2;
	
	virtual void SpudStoreCustomData_Implementation(const USpudState* State, USpudStateCustomData* CustomData) override;
	virtual void SpudRestoreCustomData_Implementation(USpudState* State, USpudStateCustomData* CustomData) override;
};


/// Simple children UObjects and parent
UCLASS()
class SPUDTEST_API UTestNestedChild1 : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	FString NestedStringVal1;
};

UCLASS()
class SPUDTEST_API UTestNestedChild2 : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	FString NestedStringVal2;
};

UCLASS()
class SPUDTEST_API UTestNestedChild3 : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	FString NestedStringVal3;
};

UCLASS()
class SPUDTEST_API UTestNestedChild4 : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	FString NestedStringVal4;
};

UCLASS()
class SPUDTEST_API UTestNestedChild5 : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	FString NestedStringVal5;
};

UCLASS()
class SPUDTEST_API UTestSaveObjectParent : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedChild1> UObjectVal1;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedChild2> UObjectVal2;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedChild3> UObjectVal3;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedChild4> UObjectVal4;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedChild5> UObjectVal5;
};

USTRUCT(BlueprintType)
struct FTestSmallStruct
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(SaveGame)
	FString StringVal;
	UPROPERTY(SaveGame)
	FText TextVal;
	UPROPERTY(SaveGame)
	float FloatVal = 0;
};

UCLASS()
class SPUDTEST_API UTestSaveObjectNonNative : public UObject
{
	GENERATED_BODY()
public:
	// Simple primitive type as control
	UPROPERTY(SaveGame)
	int IntVal;

	// Array of custom struct
	UPROPERTY(SaveGame)
	TArray<FTestSmallStruct> ArrayOfCustomStructs;

	// Map
	UPROPERTY(SaveGame)
	TMap<FString, FTestSmallStruct> Map;
	
	
};

UCLASS()
class SPUDTEST_API UTestSaveObjectRenamedClass : public UTestSaveObjectBasic
{
	GENERATED_BODY()
};

UCLASS()
class SPUDTEST_API UTestSaveObjectSlowPath : public UObject
{
	GENERATED_BODY()

public:
	// Same properties as in UTestSaveObjectBasic but shuffled

	UPROPERTY(SaveGame)
	TArray<float> FloatArray;
	UPROPERTY(SaveGame)
	TArray<double> DoubleArray;
	UPROPERTY(SaveGame)
	TArray<ETestEnum> EnumArray;
	UPROPERTY(SaveGame)
	TArray<FVector> VectorArray;
	UPROPERTY(SaveGame)
	TArray<FRotator> RotatorArray;
	UPROPERTY(SaveGame)
	TArray<FTransform> TransformArray;
	UPROPERTY(SaveGame)
	TArray<FName> NameArray;
	UPROPERTY(SaveGame)
	TArray<FString> StringArray;
	UPROPERTY(SaveGame)
	TArray<FText> TextArray;
	
	UPROPERTY(SaveGame)
	FName NameVal;
	UPROPERTY(SaveGame)
	FString StringVal;
	UPROPERTY(SaveGame)
	FText TextVal;
	
	UPROPERTY(SaveGame)
	int IntVal;
	UPROPERTY(SaveGame)
	uint8 UInt8Val;
	UPROPERTY(SaveGame)
	uint16 UInt16Val;
	UPROPERTY(SaveGame)
	uint32 UInt32Val;
	UPROPERTY(SaveGame)
	uint64 UInt64Val;

	UPROPERTY(SaveGame)
	FVector VectorVal;
	UPROPERTY(SaveGame)
	FRotator RotatorVal;
	UPROPERTY(SaveGame)
	FTransform TransformVal;
	
	UPROPERTY(SaveGame)
	int8 Int8Val;
	UPROPERTY(SaveGame)
	int16 Int16Val;
	UPROPERTY(SaveGame)
	int32 Int32Val;
	UPROPERTY(SaveGame)
	int64 Int64Val;

	UPROPERTY(SaveGame)
	float FloatVal;
	UPROPERTY(SaveGame)
	double DoubleVal;
	
	UPROPERTY(SaveGame)
	ETestEnum EnumVal;
	

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedUObject> UObjectVal;

	UPROPERTY(SaveGame)
	TObjectPtr<UTestNestedUObject> TObjectPtrVal = nullptr;

	UPROPERTY(SaveGame)
	TArray< TObjectPtr<UTestNestedUObject> > TObjectPtrArray;
	
	UPROPERTY(SaveGame)
	TArray<int> IntArray;
	UPROPERTY(SaveGame)
	TArray<uint8> UInt8Array;
	UPROPERTY(SaveGame)
	TArray<uint16> UInt16Array;
	UPROPERTY(SaveGame)
	TArray<uint32> UInt32Array;
	UPROPERTY(SaveGame)
	TArray<uint64> UInt64Array;
	UPROPERTY(SaveGame)
	TArray<int8> Int8Array;
	UPROPERTY(SaveGame)
	TArray<int16> Int16Array;
	UPROPERTY(SaveGame)
	TArray<int32> Int32Array;
	UPROPERTY(SaveGame)
	TArray<int64> Int64Array;

	UPROPERTY(SaveGame)
	TSubclassOf<AActor> ActorSubclass;

	UPROPERTY(SaveGame)
	TArray< TSubclassOf<AActor> > ActorSubclassArray;

	UPROPERTY(SaveGame)
	TMap<int, TObjectPtr<UObject>> UObjectMap;
};