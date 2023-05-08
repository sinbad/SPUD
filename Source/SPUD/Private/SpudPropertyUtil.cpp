#include "SpudPropertyUtil.h"
#include <limits>
#include "ISpudObject.h"

DEFINE_LOG_CATEGORY(LogSpudProps)

bool SpudPropertyUtil::ShouldPropertyBeIncluded(FProperty* Property, bool IsChildOfSaveGame)
{
	if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		return false;
	if (!Property->HasAnyPropertyFlags(CPF_SaveGame) && !IsChildOfSaveGame)
		return false;

	return true;
}

bool SpudPropertyUtil::IsPropertySupported(FProperty* Property)
{
	// We're going to support everything now
	return true;
}

bool SpudPropertyUtil::IsPropertyNativelySupported(FProperty* Property)
{
	if (const auto AProp = CastField<FArrayProperty>(Property))
	{
		if (!IsNativelySupportedArrayType(AProp))
			return false;
	}
	else if (const auto MProp = CastField<FMapProperty>(Property))
	{
		return false;
	}
	else if (const auto SProp = CastField<FSetProperty>(Property))
	{
		return false;
	}

	return true;
}

bool SpudPropertyUtil::IsNativelySupportedArrayType(const FArrayProperty* AProp)
{
	// We only natively support arrays of non-custom structs
	// This is because we're relying on iterating through the UObject's properties and looking up from the state data,
	// and to support this with all the issues of backwards compatibility would require such detailed offset data that
	// it would make the whole thing very unwieldy. Arrays can only be primitive types or builtin structs
	if (const auto SProp = CastField<FStructProperty>(AProp->Inner))
	{
		if (!IsBuiltInStructProperty(SProp))
			return false;
	}
	else if (IsNestedUObjectProperty(AProp->Inner))
	{
		// Same problem with nested UObjects
		return false;
	}
	return true;

}

bool SpudPropertyUtil::IsBuiltInStructProperty(const FStructProperty* SProp)
{
	return SProp->Struct == TBaseStructure<FVector>::Get() ||
        SProp->Struct == TBaseStructure<FRotator>::Get() ||
        SProp->Struct == TBaseStructure<FTransform>::Get() ||
        SProp->Struct == TBaseStructure<FGuid>::Get();

}

bool SpudPropertyUtil::IsCustomStructProperty(const FProperty* Property)
{
	if (const auto SProp = CastField<FStructProperty>(Property))
	{
		return !IsBuiltInStructProperty(SProp);
	}
	return false;
}

bool SpudPropertyUtil::IsActorObjectProperty(const FProperty* Property)
{
	// Early-out on TSubclassOf which is a specialised FObjectProperty
	if (IsSubclassOfProperty(Property))
		return false;

	if (const auto OProp = CastField<FObjectProperty>(Property))
	{
		// Raw pointers to actors
		if (OProp->PropertyClass && OProp->PropertyClass->IsChildOf(AActor::StaticClass()))
		{
			return true;
		}
	}
	// FWeakObjectPtr support
	if (const auto WProp = CastField<FWeakObjectProperty>(Property))
	{
		if (WProp->PropertyClass && WProp->PropertyClass->IsChildOf(AActor::StaticClass()))
		{
			return true;
		}
	}
	
	return false;
}

bool SpudPropertyUtil::IsNestedUObjectProperty(const FProperty* Property)
{
	// Early-out on TSubclassOf which is a specialised FObjectProperty
	if (IsSubclassOfProperty(Property))
		return false;
	
	if (const auto OProp = CastField<FObjectProperty>(Property))
	{
		if (OProp->PropertyClass && !OProp->PropertyClass->IsChildOf(AActor::StaticClass()))
		{
			return true;
		}
	}
	return false;
}

bool SpudPropertyUtil::IsSubclassOfProperty(const FProperty* Property)
{
	return CastField<FClassProperty>(Property) != nullptr;
}

uint16 SpudPropertyUtil::GetPropertyDataType(const FProperty* Prop)
{
	bool bIsArray = false;
	const FProperty* ActualProp = Prop;
	// Default to opaque record
	// Use this for arrays of custom structs, maps etc
	uint16 Ret = ESST_OpaqueRecord;

	if (const auto AProp = CastField<FArrayProperty>(Prop))
	{
		// Only natively supported array types will be stored natively
		if (IsNativelySupportedArrayType(AProp))
		{
			bIsArray = true;
			ActualProp = AProp->Inner;
		}
	}

	
	if (const auto SProp = CastField<FStructProperty>(ActualProp))
	{
		if (SProp->Struct == TBaseStructure<FVector>::Get())
			Ret = SpudTypeInfo<FVector>::EnumType;
		else if (SProp->Struct == TBaseStructure<FRotator>::Get())
			Ret = SpudTypeInfo<FRotator>::EnumType;
		else if (SProp->Struct == TBaseStructure<FTransform>::Get())
			Ret = SpudTypeInfo<FTransform>::EnumType;
		else if (SProp->Struct == TBaseStructure<FGuid>::Get())
			Ret = SpudTypeInfo<FGuid>::EnumType;
		else
			Ret = ESST_CustomStruct; // Anything else is a custom struct
	}
	else if (CastField<FObjectProperty>(ActualProp))
	{
		// Could be:
		// 1. An Actor ref
		// 2. A nested UObject
		// 3. TSubclassOf<>
		
		// Actor ref properties just have a string, which is either a name or a GUID, both strings
		// Nested UObjects and TSubclassOf are a ClassID (uint32)
		if (IsActorObjectProperty(ActualProp))
		{
			Ret = SpudTypeInfo<AActor*>::EnumType;
		}
		else if (IsSubclassOfProperty(ActualProp))
		{
			Ret = SpudTypeInfo<UClass*>::EnumType;
		}
		else
		{
			Ret = SpudTypeInfo<UObject*>::EnumType;			
		}

	}
	else
	{
		if (CastField<FBoolProperty>(ActualProp))
			Ret = SpudTypeInfo<bool>::EnumType;
		else if (CastField<FByteProperty>(ActualProp))
			Ret = SpudTypeInfo<uint8>::EnumType;
		else if (CastField<FUInt16Property>(ActualProp))
			Ret = SpudTypeInfo<uint16>::EnumType;
		else if (CastField<FUInt32Property>(ActualProp))
			Ret = SpudTypeInfo<uint32>::EnumType;
		else if (CastField<FUInt64Property>(ActualProp))
			Ret = SpudTypeInfo<uint64>::EnumType;
		else if (CastField<FInt8Property>(ActualProp))
			Ret = SpudTypeInfo<int8>::EnumType;
		else if (CastField<FInt16Property>(ActualProp))
			Ret = SpudTypeInfo<int16>::EnumType;
		else if (CastField<FIntProperty>(ActualProp))
			Ret = SpudTypeInfo<int32>::EnumType;
		else if (CastField<FInt64Property>(ActualProp))
			Ret = SpudTypeInfo<int64>::EnumType;
		else if (CastField<FFloatProperty>(ActualProp))
			Ret = SpudTypeInfo<float>::EnumType;
		else if (CastField<FDoubleProperty>(ActualProp))
			Ret = SpudTypeInfo<double>::EnumType;
		else if (CastField<FStrProperty>(ActualProp))
			Ret = SpudTypeInfo<FString>::EnumType;
		else if (CastField<FNameProperty>(ActualProp))
			Ret = SpudTypeInfo<FName>::EnumType;
		else if (CastField<FTextProperty>(ActualProp))
			Ret = SpudTypeInfo<FText>::EnumType;
		else if (CastField<FEnumProperty>(ActualProp))
			Ret = SpudTypeInfo<SpudAnyEnum>::EnumType;
	}

	if (bIsArray)
	{
		Ret |= ESST_ArrayOf;
	}
	return Ret;
	
}

FString SpudPropertyUtil::GetNestedPrefix(uint32 PrefixIDSoFar, FProperty* Prop, const FSpudClassMetadata& Meta)
{
	return (PrefixIDSoFar == SPUDDATA_PREFIXID_NONE) ? Prop->GetNameCPP() :
        Meta.GetPropertyNameFromID(PrefixIDSoFar) + "/" + Prop->GetNameCPP();
	
}

uint32 SpudPropertyUtil::FindOrAddNestedPrefixID(uint32 PrefixIDSoFar, FProperty* Prop, FSpudClassMetadata& Meta)
{
	const FString NewPrefixString = GetNestedPrefix(PrefixIDSoFar, Prop, Meta);
	return Meta.FindOrAddPropertyIDFromName(NewPrefixString);
	
}
uint32 SpudPropertyUtil::GetNestedPrefixID(uint32 PrefixIDSoFar, FProperty* Prop, const FSpudClassMetadata& Meta)
{
	const FString NewPrefixString = GetNestedPrefix(PrefixIDSoFar, Prop, Meta);
	return Meta.GetPropertyIDFromName(NewPrefixString);
	
}

void SpudPropertyUtil::RegisterProperty(uint32 PropNameID,
                                        uint32 PrefixID,
                                        uint16 DataType,
                                        TSharedPtr<FSpudClassDef> ClassDef,
                                        TArray<uint32>& PropertyOffsets,
                                        FArchive& Out)
{
	const int Index = ClassDef->FindOrAddPropertyIndex(PropNameID, PrefixID, DataType);
	if (PropertyOffsets.Num() < Index + 1)
		PropertyOffsets.SetNum(Index + 1);
	PropertyOffsets[Index] = Out.Tell();
}

void SpudPropertyUtil::RegisterProperty(const FString& Name,
                                        uint32 PrefixID,
                                        uint16 DataType,
                                        TSharedPtr<FSpudClassDef> ClassDef,
                                        TArray<uint32>& PropertyOffsets,
                                        FSpudClassMetadata& Meta,
                                        FArchive& Out)
{
	return RegisterProperty(Meta.FindOrAddPropertyIDFromName(Name), PrefixID, DataType, ClassDef, PropertyOffsets, Out);
}

void SpudPropertyUtil::RegisterProperty(FProperty* Prop,
                                        uint32 PrefixID,
                                        TSharedPtr<FSpudClassDef> ClassDef,
                                        TArray<uint32>& PropertyOffsets,
                                        FSpudClassMetadata& Meta,
                                        FArchive& Out)
{
	return RegisterProperty(Meta.FindOrAddPropertyIDFromProperty(Prop), PrefixID, GetPropertyDataType(Prop), ClassDef, PropertyOffsets, Out);
}

void SpudPropertyUtil::VisitPersistentProperties(UObject* RootObject, PropertyVisitor& Visitor, int StartDepth)
{
	VisitPersistentProperties(RootObject, RootObject->GetClass(), SPUDDATA_PREFIXID_NONE, RootObject,
	                          false, StartDepth, Visitor);
}

void SpudPropertyUtil::VisitPersistentProperties(const UStruct* Definition, PropertyVisitor& Visitor)
{
	VisitPersistentProperties(nullptr, Definition, SPUDDATA_PREFIXID_NONE, nullptr,
	                          false, 0, Visitor);
}

bool SpudPropertyUtil::VisitPersistentProperties(UObject* RootObject, const UStruct* Definition, uint32 PrefixID,
                                                     void* ContainerPtr, bool IsChildOfSaveGame, int Depth,
                                                     PropertyVisitor& Visitor)
{
	// NOTE: RootObject and ContainerPtr can be null (when parsing just definitions without instances)
	for (TFieldIterator<FProperty>PIT(Definition, EFieldIteratorFlags::IncludeSuper); PIT; ++PIT)
	{
		FProperty* Property = *PIT;

		if (!ShouldPropertyBeIncluded(Property, IsChildOfSaveGame))
			continue;

		if (!IsPropertySupported(Property))
		{
			Visitor.UnsupportedProperty(RootObject, Property, PrefixID, Depth);
			continue;
		}

		// Visitor can early-out
		if (!Visitor.VisitProperty(RootObject, Property, PrefixID, ContainerPtr, Depth))
			return false;

		// Now deal with cascading into nested structs (custom structs, not FVector etc)
		if (const auto SProp = CastField<FStructProperty>(Property))
		{
			if (!IsBuiltInStructProperty(SProp))
			{
				// Everything underneath a custom struct is recorded with a nested prefix
				const uint32 NewPrefixID = Visitor.GetNestedPrefix(SProp, PrefixID);
				// Should never have no prefix, if none abort
				if (NewPrefixID == SPUDDATA_PREFIXID_NONE)
					continue;

				const int NewDepth = Depth + 1;
				const auto StructPtr = ContainerPtr ? SProp->ContainerPtrToValuePtr<void>(ContainerPtr) : nullptr;

				Visitor.StartNestedStruct(RootObject, SProp, NewPrefixID, NewDepth);
				if (!VisitPersistentProperties(RootObject, SProp->Struct, NewPrefixID, StructPtr, true, NewDepth, Visitor))
					return false;				
				Visitor.EndNestedStruct(RootObject, SProp, NewPrefixID, NewDepth);
			}
		}

		// We no longer cascade into UObjects here, since they are separate types
		// They will be cascaded into by visitors because whether / how you cascade depends on the runtime instance type (or null)
	}

	return true;
}

uint16 SpudPropertyUtil::WriteEnumPropertyData(FEnumProperty* EProp,
                                               uint32 PrefixID,
                                               const void* Data,
                                               bool bIsArrayElement,
                                               TSharedPtr<FSpudClassDef> ClassDef,
                                               TArray<uint32>& PropertyOffsets,
                                               FSpudClassMetadata& Meta,
                                               FArchive& Out)
{
	// Enums as 16-bit numbers, that should be large enough!
	if (!bIsArrayElement)
		RegisterProperty(EProp, PrefixID, ClassDef, PropertyOffsets, Meta, Out);

	uint16 Val = EProp->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(Data);
	Out << Val;
	return Val;
}

bool SpudPropertyUtil::TryWriteEnumPropertyData(FProperty* Property,
                                                uint32 PrefixID,
                                                const void* Data,
                                                bool bIsArrayElement,
                                                int Depth,
                                                TSharedPtr<FSpudClassDef> ClassDef,
                                                TArray<uint32>& PropertyOffsets,
                                                FSpudClassMetadata& Meta,
                                                FArchive& Out)
{
	if (const auto EProp = CastField<FEnumProperty>(Property))
	{
		// Enums as 16-bit numbers, that should be large enough!
		const uint16 Val = WriteEnumPropertyData(EProp, PrefixID, Data, bIsArrayElement, ClassDef, PropertyOffsets,
		                                         Meta, Out);
		UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Property, Depth), *ToString(Val));
		return true;
	}
	return false;
}

uint16 SpudPropertyUtil::ReadEnumPropertyData(FEnumProperty* EProp, void* Data, FArchive& In)
{
	uint16 Val;
	In << Val;

	EProp->GetUnderlyingProperty()->SetIntPropertyValue(Data, static_cast<uint64>(Val));

	return Val;
}

bool SpudPropertyUtil::TryReadEnumPropertyData(FProperty* Prop, void* Data,
                                                     const FSpudPropertyDef& StoredProperty,
                                                     int Depth, FArchive& In)
{
	auto EProp = CastField<FEnumProperty>(Prop);
	if (EProp && StoredPropertyTypeMatchesRuntime(Prop, StoredProperty, true))
		// we ignore array flag since we could be processing inner
	{
		// Enums as 16-bit numbers, that should be large enough!
		const uint16 Val = ReadEnumPropertyData(EProp, Data, In);
		UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Prop, Depth), *ToString(Val));
		return true;
	}
	return false;
}

FString SpudPropertyUtil::WriteActorRefPropertyData(FProperty* OProp,
                                                    AActor* Actor,
                                                    uint32 PrefixID,
                                                    const void* Data,
                                                    bool bIsArrayElement,
                                                    TSharedPtr<FSpudClassDef> ClassDef,
                                                    TArray<uint32>& PropertyOffsets,
                                                    FSpudClassMetadata& Meta,
                                                    FArchive& Out)
{
	if (!bIsArrayElement)
		RegisterProperty(OProp, PrefixID, ClassDef, PropertyOffsets, Meta, Out);

	FString RefString;
	// We already have the Actor so no need to get property value
	if (Actor)
	{
		if (IsRuntimeActor(Actor))
		{
			// For runtime objects, we need GUID
			auto GuidProperty = FindGuidProperty(Actor);
			if (!GuidProperty)
			{
				UE_LOG(LogSpudProps, Error, TEXT("Object reference %s/%s points to runtime Actor %s but that actor has no SpudGuid property, will not be saved."),
                    *ClassDef->ClassName, *OProp->GetName(), *Actor->GetName());
				// This essentially becomes a null reference
				RefString = FString();				
			}
			else
			{
				FGuid Guid = GetGuidProperty(Actor, GuidProperty);
				if (!Guid.IsValid())
				{
					// We automatically generate a Guid for any referenced object if it doesn't have one already
					Guid = FGuid::NewGuid();
					SetGuidProperty(Actor, GuidProperty, Guid);
				}
				// We write the GUID as {00000000-0000-0000-0000-000000000000} format so that it's easy to detect when loading
				// vs an object name (first char is open brace)
				RefString = Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces);
			}
		}
		else
		{
			// References to level actors uses their unique name (so no need for a SpudGuid property)
			RefString = GetLevelActorName(Actor);
		}
			
	}
	else
		RefString = FString();
	
	Out << RefString;
	return RefString;
}

FString SpudPropertyUtil::WriteNestedUObjectPropertyData(FObjectProperty* OProp,
                                                         UObject* UObj,
                                                         uint32 PrefixID,
                                                         const void* Data,
                                                         bool bIsArrayElement,
                                                         TSharedPtr<FSpudClassDef> ClassDef,
                                                         TArray<uint32>& PropertyOffsets,
                                                         FSpudClassMetadata& Meta,
                                                         FArchive& Out)
{
	if (!bIsArrayElement)
		RegisterProperty(OProp, PrefixID, ClassDef, PropertyOffsets, Meta, Out);

	uint32 ClassID;
	FString Ret = "NULL";
	// We already have the Actor so no need to get property value
	if (UObj)
	{		
		// UObjects (not actor refs) first store the class (as an ID)
		Ret = GetClassName(UObj);
		ClassID = Meta.FindOrAddClassIDFromName(Ret);
	}
	else // null
		ClassID = SPUDDATA_CLASSID_NONE;
	
	Out << ClassID;

	// Note that we ONLY write the class (or null) here. Actual property data is cascaded separately
	return Ret;
}

FString SpudPropertyUtil::WriteSubclassOfPropertyData(FClassProperty* CProp,
                                                      UClass* Class,
                                                      uint32 PrefixID,
                                                      const void* Data,
                                                      bool bIsArrayElement,
                                                      TSharedPtr<FSpudClassDef> ClassDef,
                                                      TArray<uint32>& PropertyOffsets,
                                                      FSpudClassMetadata& Meta,
                                                      FArchive& Out)
{
	if (!bIsArrayElement)
		RegisterProperty(CProp, PrefixID, ClassDef, PropertyOffsets, Meta, Out);

	uint32 ClassID;
	FString Ret = "NULL";
	if (Class)
	{		
		// We only need to store the class ID
		Ret = Class->GetPathName();
		ClassID = Meta.FindOrAddClassIDFromName(Ret);
	}
	else // null
		ClassID = SPUDDATA_CLASSID_NONE;
	
	Out << ClassID;

	return Ret;
}

bool SpudPropertyUtil::TryWriteUObjectPropertyData(FProperty* Property,
                                                   uint32 PrefixID,
                                                   const void* Data,
                                                   bool bIsArrayElement,
                                                   int Depth,
                                                   TSharedPtr<FSpudClassDef> ClassDef,
                                                   TArray<uint32>& PropertyOffsets,
                                                   FSpudClassMetadata& Meta,
                                                   FArchive& Out)
{
	UObject* Obj = nullptr;
	FObjectProperty* StrongProp = CastField<FObjectProperty>(Property);
	FWeakObjectProperty* WeakProp = CastField<FWeakObjectProperty>(Property);
	
	if (StrongProp)
	{
		Obj = StrongProp->GetObjectPropertyValue(Data);
	}
	else if (WeakProp)
	{
		Obj = WeakProp->GetObjectPropertyValue(Data);
	}

	if (StrongProp || WeakProp)
	{
		// Nullrefs are OK, but if valid we need to check it's an Actor
		if (IsActorObjectProperty(Property))
		{
			const auto Actor = Cast<AActor>(Obj);			
			const FString Val = WriteActorRefPropertyData(Property, Actor, PrefixID, Data, bIsArrayElement, ClassDef,
			                                              PropertyOffsets, Meta, Out);
			UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Property, Depth), *ToString(Val));
		}
		else if (auto CProp = CastField<FClassProperty>(Property))
		{
			const auto RuntimeClass = Cast<UClass>(Obj);
			const FString Val = WriteSubclassOfPropertyData(CProp, RuntimeClass, PrefixID, Data, bIsArrayElement, ClassDef,
			                                                PropertyOffsets, Meta, Out);
			UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Property, Depth), *Val);
		}
		else if (StrongProp) // Only strong properties on nested (should be owned)
		{
			// non-actor UObject
			const FString Val = WriteNestedUObjectPropertyData(StrongProp, Obj, PrefixID, Data, bIsArrayElement, ClassDef,
			                                                   PropertyOffsets, Meta, Out);
			UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Property, Depth), *Val);
		}
		return true;
	}
	return false;
}


FString SpudPropertyUtil::ReadActorRefPropertyData(FProperty* OProp,
                                                   void* Data,
                                                   const RuntimeObjectMap* RuntimeObjects,
                                                   ULevel* Level,
                                                   FArchive& In)
{
	FString RefString;
	In << RefString;

	// Now we need to find the actual object
	if (RefString.IsEmpty())
	{
		SetObjectPropertyValue(OProp, Data, nullptr);
	}
	else if (RefString.StartsWith("{"))
	{
		// Runtime object, identified by GUID
		// We used the braces-format GUID for runtime objects so that it's easy to identify
		if (RuntimeObjects)
		{
			FGuid Guid;
			if (FGuid::ParseExact(RefString, EGuidFormats::DigitsWithHyphensInBraces, Guid))
			{
				auto ObjPtr = RuntimeObjects->Find(Guid);
				if (ObjPtr)
				{
					SetObjectPropertyValue(OProp, Data, *ObjPtr);
				}
				else
				{
					UE_LOG(LogSpudProps, Error, TEXT("Could not locate runtime object for property %s, GUID was %s"), *OProp->GetName(), *RefString);	
				}			
			}
			else
			{
				UE_LOG(LogSpudProps, Error, TEXT("Error parsing GUID %s for property %s"), *RefString, *OProp->GetName());
			}
		}
		else
			UE_LOG(LogSpudProps, Error, TEXT("Found property reference to runtime object %s->%s but no RuntimeObjects passed (global object?)"), *OProp->GetName(), *RefString);
	}
	else
	{
		// Level object, identified by name. Level is the package
		if (Level)
		{
			auto Obj = StaticFindObjectFast(AActor::StaticClass(), Level, *RefString);
			if (!Obj)
			{
				// Not found in owning level, search all
				for (auto OtherLevel : Level->GetWorld()->GetLevels())
				{
					if (OtherLevel == Level)
						continue;
					Obj = StaticFindObjectFast(AActor::StaticClass(), OtherLevel, *RefString);
					if (Obj)
						break;
				}
			}
			if (Obj)
			{
				SetObjectPropertyValue(OProp, Data, Obj);
			}
			else
			{
				UE_LOG(LogSpudProps, Error, TEXT("Could not locate level object for property %s, name was %s"), *OProp->GetName(), *RefString);	
			}
		}
		else
		{
			UE_LOG(LogSpudProps, Error, TEXT("Level object for property %s cannot be resolved, null parent Level"), *OProp->GetName());	
		}
		
	}
	return RefString;
}

FString SpudPropertyUtil::ReadNestedUObjectPropertyData(FObjectProperty* OProp,
                                                        void* Data,
                                                        const RuntimeObjectMap* RuntimeObjects,
                                                        ULevel* Level,
                                                        UObject* Outer,
                                                        const FSpudClassMetadata& Meta,
                                                        FArchive& In)
{
	uint32 ClassID;
	In << ClassID;

	UObject* Object = nullptr;
	FString Ret = "NULL";
	
	if (ClassID == SPUDDATA_CLASSID_NONE)
	{
		// If stored data said it should be null, set it
		OProp->SetObjectPropertyValue(Data, nullptr);
	}
	else
	{
		// If stored data is non-null, instantiate if needed
		// Only instantiate if null, to allow user code to instantiate subclasses of property type if required
		if (!IsValid(OProp->GetObjectPropertyValue(Data)))
		{
			const FString ClassName = Meta.GetClassNameFromID(ClassID);

			const FSoftClassPath CP(ClassName);
			const auto Class = CP.TryLoadClass<UObject>();

			if (!Class)
			{
				UE_LOG(LogSpudProps, Error, TEXT("Cannot respawn instance of %s, class not found"), *ClassName);
				return Ret;
			}

			Object = NewObject<UObject>(Outer, Class);
			OProp->SetObjectPropertyValue(Data, Object);
			Ret = ClassName;
		}
		// Otherwise, we leave the existing instance there
		// Nested properties will be re-populated as before during cascade
	}

	return Ret;
}

FString SpudPropertyUtil::ReadSubclassOfPropertyData(FClassProperty* CProp,
                                                     void* Data,
                                                     const RuntimeObjectMap* RuntimeObjects,
                                                     ULevel* Level,
                                                     const FSpudClassMetadata& Meta,
                                                     FArchive& In)
{
	// TSubclassOf is just a class ID
	uint32 ClassID;
	In << ClassID;

	FString Ret = "NULL";
	if (ClassID == SPUDDATA_CLASSID_NONE)
	{
		// If stored data said it should be null, set it
		CProp->SetObjectPropertyValue(Data, nullptr);
	}
	else
	{
		const FString ClassName = Meta.GetClassNameFromID(ClassID);

		const FSoftClassPath CP(ClassName);
		const auto Class = CP.TryLoadClass<UObject>();

		if (!Class)
		{
			UE_LOG(LogSpudProps, Error, TEXT("Cannot find class %s"), *ClassName);
			return Ret;
		}

		// For a FClassProperty, the object value is the class instance
		CProp->SetObjectPropertyValue(Data, Class);
		Ret = ClassName;
	}

	return Ret;
	
}


bool SpudPropertyUtil::TryReadUObjectPropertyData(FProperty* Prop, void* Data,
                                                  const FSpudPropertyDef& StoredProperty, const RuntimeObjectMap* RuntimeObjects, ULevel* Level, UObject* Outer,
                                                  const FSpudClassMetadata& Meta, int Depth, FArchive& In)
{
	FObjectProperty* StrongProp = CastField<FObjectProperty>(Prop);
	FWeakObjectProperty* WeakProp = CastField<FWeakObjectProperty>(Prop);
	
	
	if ((StrongProp || WeakProp) && StoredPropertyTypeMatchesRuntime(Prop, StoredProperty, true)) // we ignore array flag since we could be processing inner
	{

		// Nullrefs are OK, but if valid we need to check it's an Actor
		// Actor refs supports both strong & weak object refs
		if (IsActorObjectProperty(Prop))
		{
			const FString Val = ReadActorRefPropertyData(Prop, Data, RuntimeObjects, Level, In);
			UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Prop, Depth), *Val);
		}
		else if (auto CProp = CastField<FClassProperty>(Prop))
		{
			const FString Val = ReadSubclassOfPropertyData(CProp, Data, RuntimeObjects, Level, Meta, In);
			UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Prop, Depth), *Val);
		}
		else if (StrongProp) // Only strong refs for nested (owned)
		{
			const FString Val = ReadNestedUObjectPropertyData(StrongProp, Data, RuntimeObjects, Level, Outer, Meta, In);
			UE_LOG(LogSpudProps, Verbose, TEXT("%s = %s"), *GetLogPrefix(Prop, Depth), *Val);
		}
		return true;
			
	}
	return false;
	
}

void SpudPropertyUtil::SetObjectPropertyValue(FProperty* Property, void* Data, UObject* Obj)
{
	// Support setting strong and weak pointers
	if (const auto OProp = CastField<FObjectProperty>(Property))
	{
		OProp->SetObjectPropertyValue(Data, Obj);
	}
	else if (const auto WProp = CastField<FWeakObjectProperty>(Property))
	{
		WProp->SetObjectPropertyValue(Data, Obj);
	}
}

void SpudPropertyUtil::StoreProperty(const UObject* RootObject,
                                     FProperty* Property,
                                     uint32 PrefixID,
                                     const void* ContainerPtr,
                                     int Depth,
                                     TSharedPtr<FSpudClassDef> ClassDef,
                                     TArray<uint32>& PropertyOffsets,
                                     FSpudClassMetadata& Meta,
                                     FMemoryWriter& Out)
{
	// Arrays supported, but not maps / sets yet
	if (const auto AProp = CastField<FArrayProperty>(Property))
	{
		if (IsNativelySupportedArrayType(AProp))
		{
			StoreArrayProperty(AProp, RootObject, PrefixID, ContainerPtr, Depth, ClassDef, PropertyOffsets, Meta, Out);
			return;
		}
	}

	// Includes arrays of custom structs, maps etc
	StoreContainerProperty(Property, RootObject, PrefixID, ContainerPtr, false, Depth, ClassDef, PropertyOffsets, Meta, Out);
}

void SpudPropertyUtil::StoreArrayProperty(FArrayProperty* AProp,
                                          const UObject* RootObject,
                                          uint32 PrefixID,
                                          const void* ContainerPtr,
                                          int Depth,
                                          TSharedPtr<FSpudClassDef> ClassDef,
                                          TArray<uint32>& PropertyOffsets,
                                          FSpudClassMetadata& Meta,
                                          FMemoryWriter& Out)
{
	
	// Use helper to get number, ArrayDim doesn't seem to work?
	const void* DataPtr = AProp->ContainerPtrToValuePtr<void>(ContainerPtr);
	FScriptArrayHelper ArrayHelper(AProp, DataPtr);
	const int32 NumElements = ArrayHelper.Num();

	if (NumElements > std::numeric_limits<uint16>::max())
	{
		UE_LOG(LogSpudProps, Error, TEXT("Array property %s/%s has %d elements, exceeds maximum of %d, will be truncated"),
			*RootObject->GetName(), *AProp->GetName(), NumElements, std::numeric_limits<uint16>::max());
	}

	RegisterProperty(AProp, PrefixID, ClassDef, PropertyOffsets, Meta, Out);
	
	// Data is count first, then elements
	uint16 ShortElems = static_cast<uint16>(NumElements);
	Out << ShortElems;
	for (int ArrayElem = 0; ArrayElem < NumElements; ++ArrayElem)
	{
		void *ElemPtr = ArrayHelper.GetRawPtr(ArrayElem);
		StoreContainerProperty(AProp->Inner, RootObject, PrefixID, ElemPtr, true, Depth, ClassDef, PropertyOffsets, Meta, Out);
	}
	
}

void SpudPropertyUtil::StoreContainerProperty(FProperty* Property,
                                              const UObject* RootObject,
                                              uint32 PrefixID,
                                              const void* ContainerPtr,
                                              bool bIsArrayElement,
                                              int Depth,
                                              TSharedPtr<FSpudClassDef> ClassDef,
                                              TArray<uint32>& PropertyOffsets,
                                              FSpudClassMetadata& Meta,
                                              FMemoryWriter& Out)
{
	bool bUpdateOK;
	if (IsPropertyNativelySupported(Property))
	{
		// Get pointer to data within container, must be from original property in the case of arrays
		const void* DataPtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		if (const auto SProp = CastField<FStructProperty>(Property))
		{
			if (IsBuiltInStructProperty(SProp))
			{
				// Builtin structs
				bUpdateOK =
					TryWriteBuiltinStructPropertyData<FVector>(SProp, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
					TryWriteBuiltinStructPropertyData<FRotator>(SProp, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
					TryWriteBuiltinStructPropertyData<FTransform>(SProp, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
					TryWriteBuiltinStructPropertyData<FGuid>(SProp, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out);
			}
			else
			{
				// We assume that nested custom structs are ok
				// We don't cascade here any more, visitor does it
				// Just log fo consistency
				UE_LOG(LogSpudProps, Verbose, TEXT("%s:"), *GetLogPrefix(Property, Depth));
				bUpdateOK = true;
			}
		}
		else 
		{
			bUpdateOK =
				TryWritePropertyData<FBoolProperty,		bool>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FByteProperty,		uint8>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FUInt16Property,	uint16>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FUInt32Property,	uint32>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FUInt64Property,	uint64>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FInt8Property,		int8>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FInt16Property,	int16>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FIntProperty,		int>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FInt64Property,	int64>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FFloatProperty,	float>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FDoubleProperty,	double>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FStrProperty,		FString>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FNameProperty,		FName>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWritePropertyData<FTextProperty,		FText>(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWriteEnumPropertyData(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out) ||
				TryWriteUObjectPropertyData(Property, PrefixID, DataPtr, bIsArrayElement, Depth, ClassDef, PropertyOffsets, Meta, Out);;
		
		}
	}
	else
	{
		// Not a fully supported type, wrap in an FRecord as an opaque type
		// Not super efficient but useful for plugging gaps in things we support
		RegisterProperty(Property, PrefixID, ClassDef, PropertyOffsets, Meta, Out);
		FBinaryArchiveFormatter Fmt(Out);
		FStructuredArchive Ar(Fmt);
		const auto RootSlot = Ar.Open();
		// Nasty const cast but that's because this loads and saves
		Property->SerializeBinProperty(RootSlot, const_cast<void*>(ContainerPtr));
		Ar.Close();
		bUpdateOK = true;
	}
	if (!bUpdateOK)
	{
		UE_LOG(LogSpudProps, Error, TEXT("Unable to update from property %s, unsupported type."), *Property->GetName());
	}
	
}

void SpudPropertyUtil::RestoreProperty(UObject* RootObject, FProperty* Property, void* ContainerPtr,
                                             const FSpudPropertyDef& StoredProperty,
                                             const RuntimeObjectMap* RuntimeObjects,
                                             const FSpudClassMetadata& Meta,
                                             int Depth,
                                             FMemoryReader& DataIn)
{
	// Arrays supported, but not maps / sets yet
	if (const auto AProp = CastField<FArrayProperty>(Property))
	{
		if (IsNativelySupportedArrayType(AProp))
		{
			RestoreArrayProperty(RootObject, AProp, ContainerPtr, StoredProperty, RuntimeObjects, Meta, Depth, DataIn);
			return;
		}
	}
		
	// Otherwise pass through general property util
	RestoreContainerProperty(RootObject,Property, ContainerPtr, StoredProperty, RuntimeObjects, Meta, Depth, DataIn);
}


void SpudPropertyUtil::RestoreArrayProperty(UObject* RootObject, FArrayProperty* const AProp,
                                                  void* ContainerPtr, const FSpudPropertyDef& StoredProperty,
                                                  const RuntimeObjectMap* RuntimeObjects,
                                                  const FSpudClassMetadata& Meta,
                                                  int Depth,
                                                  FMemoryReader& DataIn)
{

	// Array properties store the count as a uint16 first
	uint16 NumElems;
	DataIn << NumElems;
	
	void* DataPtr = AProp->ContainerPtrToValuePtr<void>(ContainerPtr);
	FScriptArrayHelper ArrayHelper(AProp, DataPtr);
	ArrayHelper.Resize(NumElems);

	// After that, it's just like restoring a single property, just to a new location for each element
	for (int ArrayElem = 0; ArrayElem < NumElems; ++ArrayElem)
	{
		void *ElemPtr = ArrayHelper.GetRawPtr(ArrayElem);
		RestoreContainerProperty(RootObject, AProp->Inner, ElemPtr, StoredProperty, RuntimeObjects, Meta, Depth, DataIn);
	}
	
}

void SpudPropertyUtil::RestoreContainerProperty(UObject* RootObject, FProperty* const Property,
                                                      void* ContainerPtr, const FSpudPropertyDef& StoredProperty,
                                                      const RuntimeObjectMap* RuntimeObjects,
                                                      const FSpudClassMetadata& Meta,
                                                      int Depth,
                                                      FMemoryReader& DataIn)
{
	bool bUpdateOK;

	if (IsPropertyNativelySupported(Property))
	{
		// Get pointer to data within container, must be from original property in the case of arrays
		void* DataPtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		if (const auto SProp = CastField<FStructProperty>(Property))
		{
			if (IsBuiltInStructProperty(SProp))
			{
				// Builtin structs
				bUpdateOK =
					TryReadBuiltinStructPropertyData<FVector>(SProp, DataPtr, StoredProperty, Depth, DataIn) ||
					TryReadBuiltinStructPropertyData<FRotator>(SProp, DataPtr, StoredProperty, Depth, DataIn) ||
					TryReadBuiltinStructPropertyData<FTransform>(SProp, DataPtr, StoredProperty, Depth, DataIn) ||
					TryReadBuiltinStructPropertyData<FGuid>(SProp, DataPtr, StoredProperty, Depth, DataIn);
			}
			else
			{
				// We assume that nested custom structs are ok
				// We don't cascade here any more, visitor does it
				bUpdateOK = true;
			}
		}
		else 
		{
			bUpdateOK =
				TryReadPropertyData<FBoolProperty,		bool>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FByteProperty,		uint8>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FUInt16Property,	uint16>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FUInt32Property,	uint32>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FUInt64Property,	uint64>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FInt8Property,		int8>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FInt16Property,	int16>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FIntProperty,		int>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FInt64Property,	int64>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FFloatProperty,	float>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FDoubleProperty,	double>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FStrProperty,		FString>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FNameProperty,		FName>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadPropertyData<FTextProperty,		FText>(Property, DataPtr, StoredProperty, Depth, DataIn) ||
				TryReadEnumPropertyData(Property, DataPtr, StoredProperty, Depth, DataIn);

			if (!bUpdateOK)
			{
				// Actors can refer to each other
			
				ULevel* Level = nullptr;
				if (auto Actor = Cast<AActor>(RootObject))
				{
					Level = Actor->GetLevel();
				}
				if (!IsValid(Level))
				{
					if (UActorComponent* ActorComponentLevelCheck = Cast<UActorComponent>(RootObject))
					{
						Level = ActorComponentLevelCheck->GetOwner()->GetLevel();
					}
				}
				bUpdateOK = TryReadUObjectPropertyData(Property, DataPtr, StoredProperty, RuntimeObjects, Level, RootObject, Meta, Depth, DataIn);
			}
		
		}
	}
	else
	{
		// Not a fully supported type, will have been wrapped in an FRecord as an opaque type
		FBinaryArchiveFormatter Fmt(DataIn);
		FStructuredArchive Ar(Fmt);
		const auto RootSlot = Ar.Open();
		// Nasty const cast but that's because this loads and saves
		Property->SerializeBinProperty(RootSlot, ContainerPtr);
		Ar.Close();
		bUpdateOK = true;
		
	}
	if (!bUpdateOK)
	{
		UE_LOG(LogSpudProps, Error, TEXT("Unable to restore property %s, unsupported type."), *Property->GetName());
	}
	
}

bool SpudPropertyUtil::StoredClassDefMatchesRuntime(const FSpudClassDef& ClassDef, const FSpudClassMetadata& Meta)
{
	// This implementation needs to iterate / recurse in *exactly* the same way as the Store methods for the same
	// Class. The visitor pattern ensures that.
	// We *could* generate a hash of properties to compare stored to runtime, but since we'd have to generate the runtime
	// hash every time anyway, it's actually quicker to just iterate and fail on the first non-match than calculate an entire hash
	// We'll cache this result per file load anyway
	const FSoftClassPath CP(ClassDef.ClassName);
	const auto RuntimeClass = CP.TryLoadClass<UObject>();

	const auto StoredPropertyIterator = ClassDef.Properties.CreateConstIterator();

	StoredMatchesRuntimePropertyVisitor Visitor(StoredPropertyIterator, ClassDef, Meta);
	SpudPropertyUtil::VisitPersistentProperties(RuntimeClass, Visitor);

	return Visitor.IsMatch();
}

bool SpudPropertyUtil::StoredPropertyTypeMatchesRuntime(const FProperty* RuntimeProperty, const FSpudPropertyDef& StoredProperty, bool bIgnoreArrayFlag)
{
	uint16 StoredType = StoredProperty.DataType;
	uint16 RuntimeType = GetPropertyDataType(RuntimeProperty);
	if (bIgnoreArrayFlag)
	{
		StoredType = StoredType & ~ESST_ArrayOf;
		RuntimeType = RuntimeType & ~ESST_ArrayOf;
	}
		
	return StoredType == RuntimeType;
}

SpudPropertyUtil::StoredMatchesRuntimePropertyVisitor::StoredMatchesRuntimePropertyVisitor(
	TArray<FSpudPropertyDef>::TConstIterator InStoredPropertyIterator, 
	const FSpudClassDef& InClassDef, const FSpudClassMetadata& InMeta):
	StoredPropertyIterator(InStoredPropertyIterator),
	ClassDef(InClassDef),
	Meta(InMeta),
	bMatches(true) // assume matching until we know otherwise
{
}

bool SpudPropertyUtil::StoredMatchesRuntimePropertyVisitor::VisitProperty(UObject* RootObject, FProperty* RuntimeProperty,
	uint32 CurrentPrefixID, void* ContainerPtr, int Depth)
{
	if (const auto SProp = CastField<FStructProperty>(RuntimeProperty))
	{
		if (!IsBuiltInStructProperty(SProp))
		{
			// Struct entry itself is not recorded, only nested fields, no need to check
			// Visitor will call us with the nested properties
			return true;
		}
		// Builtin structs (FVector etc) are just like any other property, proceed
	}

	// The next property we encounter from the Container should match the next item on
	// StoredPropertyIterator. We increment it as well!
	if (!StoredPropertyIterator)
	{
		// Ran out of stored properties early, so doesn't match
		bMatches = false;
		return false;
	}

	auto& StoredProperty = *StoredPropertyIterator++;

	if (StoredProperty.DataType == SpudTypeInfo<UObject*>::EnumType)
	{
		// This is an odd case. We embed nested UObject properties inside the parent like a struct, but
		// when checking the runtime class, there is no instance associated with it so we can't cascade
		// Also because a UObject property can hold a subclass with extra properties, there may be more properties
		// in the data than in the UClass of the property type. So, to get around this we don't cascade during the
	}
	

	// Wrong struct nesting (ID comes from stored record of prefix matched with struct name on parent call)
	if (CurrentPrefixID != StoredProperty.PrefixID)
	{
		UE_LOG(LogSpudProps, Verbose, TEXT("StoredClassDefMatchesRuntime: Prefix mismatch %s/%s: %d != %d"),
		       *RuntimeProperty->GetClass()->GetName(), *RuntimeProperty->GetNameCPP(), StoredProperty.PrefixID, CurrentPrefixID);
		bMatches = false;
		return false; // causes caller to early-out
	}

	const FString& StoredPropName = Meta.GetPropertyNameFromID(StoredProperty.PropertyID);
	// Check name, ignoring case
	if (!StoredPropName.Equals(RuntimeProperty->GetNameCPP(), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogSpudProps, Verbose, TEXT("StoredClassDefMatchesRuntime: Name mismatch for %s: %s != %s"),
		       *RuntimeProperty->GetClass()->GetName(), *StoredPropName, *RuntimeProperty->GetNameCPP());
		bMatches = false;
		return false; // causes caller to early-out
	}

	// Check type
	if (!StoredPropertyTypeMatchesRuntime(RuntimeProperty, StoredProperty, false))
	{
		UE_LOG(LogSpudProps, Verbose, TEXT("StoredClassDefMatchesRuntime: Type mismatch %s/%s: %d != %d"),
		       *RuntimeProperty->GetClass()->GetName(), *RuntimeProperty->GetNameCPP(), StoredProperty.DataType,
		       GetPropertyDataType(RuntimeProperty));
		bMatches = false;
		return false; // causes caller to early-out
	}

	return true;
	
}

uint32 SpudPropertyUtil::StoredMatchesRuntimePropertyVisitor::GetNestedPrefix(
	FProperty* Prop, uint32 CurrentPrefixID)
{
	// This doesn't create a new ID, expects it to be there already
	return GetNestedPrefixID(CurrentPrefixID, Prop, Meta);
}
bool SpudPropertyUtil::IsRuntimeActor(const AActor* Actor)
{
	// RF_WasLoaded means it was part of a level
	// But not being part of a level might not means it needs to be respawned, it might have been
	// auto-spawned e.g. Game Modes, pawns

	bool Ret = IsValid(Actor) && !Actor->HasAnyFlags(RF_WasLoaded);

	// Interestingly, objects which have been created in the editor but the level hasn't been saved yet will
	// cause the object to incorrectly be marked as spawned after level load, because RF_WasLoaded is false
	// In fact the only flags these unsaved objects have is RF_Transactional, but all actors have that
	// We can detect this by checking if the package is dirty, but this can only really be done in an editor lib
	// to avoid circular references.
	

	return Ret;
}

bool SpudPropertyUtil::IsPersistentObject(UObject* Obj)
{
	return IsValid(Obj) && Obj->Implements<USpudObject>();
}

FStructProperty* SpudPropertyUtil::FindGuidProperty(const UObject* Obj)
{
	for (TFieldIterator<FProperty> It(Obj->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (const auto SProp = CastField<FStructProperty>(Property))
		{
			if (SProp->Struct == TBaseStructure<FGuid>::Get() &&
                SProp->GetName().Equals("SpudGuid"))
			{
				return SProp;
			}
		}
	}
	return nullptr;
}

FGuid SpudPropertyUtil::GetGuidProperty(const UObject* Obj)
{
	return GetGuidProperty(Obj, FindGuidProperty(Obj));
}

FGuid SpudPropertyUtil::GetGuidProperty(const UObject* Obj, const FStructProperty* Prop)
{
	FGuid Ret;
	if (Prop)
	{		
		Ret = *Prop->ContainerPtrToValuePtr<FGuid>(Obj);
	}
	return Ret;
}

bool SpudPropertyUtil::SetGuidProperty(UObject* Obj, const FGuid& Guid)
{
	return SetGuidProperty(Obj, FindGuidProperty(Obj), Guid);
}

bool SpudPropertyUtil::SetGuidProperty(UObject* Obj, const FStructProperty* Prop, const FGuid& Guid)
{
	if (Prop)
	{		
		auto GuidPtr = Prop->ContainerPtrToValuePtr<FGuid>(Obj);
		*GuidPtr = Guid;
		return true;
	}
	return false;
}

FString SpudPropertyUtil::GetLevelActorName(const AActor* Actor)
{
	if (Actor->Implements<USpudObject>())
	{
		auto Name = ISpudObject::Execute_OverrideName(Actor);
		if (!Name.IsEmpty())
			return Name;
	}
	
	return Actor->GetFName().ToString();
}

FString SpudPropertyUtil::GetGlobalObjectID(const UObject* Obj)
{
	const auto Guid = SpudPropertyUtil::GetGuidProperty(Obj);
	if (Guid.IsValid())
		return Guid.ToString(SPUDDATA_GUID_KEY_FORMAT);
	else
		return Obj->GetFName().ToString();	
}

FString SpudPropertyUtil::GetClassName(const UObject* Obj)
{
	// Full class name allows for re-spawning
	// E.g. /Game/Blueprints/Class.Blah_C
	if (Obj)
		return Obj->GetClass()->GetPathName();
	
	return FString();
}

FString SpudPropertyUtil::GetLogPrefix(int Depth)
{
	const FString Prefix = FString::ChrN(Depth, '-');
	return FString::Printf(TEXT(" |%s"), *Prefix);
}

FString SpudPropertyUtil::GetLogPrefix(const FProperty* Property, int Depth)
{
	const FString Prefix = FString::ChrN(Depth, '-');

	return FString::Printf(TEXT(" |%s %s"), *Prefix, *Property->GetNameCPP());
}


