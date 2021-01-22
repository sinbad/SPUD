#pragma once

#include "CoreMinimal.h"

#include "ISpudObject.h"
#include "GameFramework/Character.h"

#include "SpudHelpers.generated.h"

// You don't have to use one of these base classes for your actors; instead you can just modify your
// own classes to:
// 1. Implement the ISpudObject interface to opt instances in to persistence
// 2. Optionally add a property of type FGuid called "SpudGuid", which you must initialise on construction.
//    This is required if you need to save runtime-created instances of the class (as opposed to objects in levels),
//    or you want to be able to save references to instances of the class.

/// Helper replacement for AActor as a base class which includes the ISpudObject marker
/// interface, and also the SpudGuid property. This property allows for persistence of:
/// 1. Runtime-created objects (vs objects present in levels)
/// 2. Persistent cross-references between object instances
UCLASS(Blueprintable)
class SPUD_API ASpudActorBase : public AActor, public ISpudObject
{
	GENERATED_BODY()
public:
	/// GUID required to successfully persist runtime-created objects
	/// This will be generated on save if necessary, or you can initialise it yourself
	UPROPERTY(BlueprintReadOnly)
	FGuid SpudGuid;
};

/// Helper replacement for APawn as a base class which includes the ISpudObject marker
/// interface, and also the SpudGuid property. This property allows for persistence of:
/// 1. Runtime-created objects (vs objects present in levels)
/// 2. Persistent cross-references between object instances
UCLASS(Blueprintable)
class SPUD_API ASpudPawnBase : public APawn, public ISpudObject
{
	GENERATED_BODY()
public:
	/// GUID required to identify runtime-created objects
	/// This will be generated on save if necessary, or you can initialise it yourself
	UPROPERTY(BlueprintReadOnly)
	FGuid SpudGuid;
};

/// Helper replacement for APawn as a base class which includes the ISpudObject marker
/// interface, and also the SpudGuid property. This property allows for persistence of:
/// 1. Runtime-created objects (vs objects present in levels)
/// 2. Persistent cross-references between object instances
UCLASS(Blueprintable)
class SPUD_API ASpudCharacterBase : public ACharacter, public ISpudObject
{
	GENERATED_BODY()
public:
	/// GUID required to identify runtime-created objects
	/// This will be generated on save if necessary, or you can initialise it yourself
	UPROPERTY(BlueprintReadOnly)
	FGuid SpudGuid;
	
};
