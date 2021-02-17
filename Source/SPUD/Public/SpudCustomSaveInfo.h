#pragma once

#include "CoreMinimal.h"

#include "SpudData.h"
#include "SpudPropertyUtil.h"



#include "SpudCustomSaveInfo.generated.h"

/// Helper class to allow users to apply custom properties to the header information of save files,
/// which can then be read back on save/load screens without loading the entire save.
/// Could be things like completion percentage, hours played, current quests, character class, character level etc
UCLASS(BlueprintType)
class SPUD_API USpudCustomSaveInfo : public UObject
{
	GENERATED_BODY()
protected:
	FSpudSaveCustomInfo Data;

	int GetPropertyIndex(const FString& Name)
	{
		for (int i = 0; i < Data.PropertyNames.Num(); ++i)
		{
			if (Data.PropertyNames[i] == Name)
				return i;
		}

		return -1;
	}

	/// Set a value to the data
	/// NOTE: if it already exists the value MUST slot in to replace
	/// For variable length types, call SetVariableLength instead
	template <typename T>
    void Set(const FString& Name, const T& Value)
	{
		const int i =  GetPropertyIndex(Name);
		uint32 Offset;
		if (i < 0)
		{
			// Add a new entry
			Offset = Data.PropertyData.Num();
			Data.PropertyNames.Add(Name);
			Data.PropertyOffsets.Add(Offset);
		}
		else
		{
			// Update in-place (note this will NEVER happen for variable length types)
			Offset = Data.PropertyOffsets[i];
		}
		
		FMemoryWriter Ar(Data.PropertyData);
		Ar.Seek(Offset);
		SpudPropertyUtil::WriteRaw(Value, Ar);
	}

	/// Try to get a value from the custom data
	template <typename T>
    bool Get(const FString& Name, T& OutValue)
	{
		const int i =  GetPropertyIndex(Name);
		if (i >= 0)
		{
			const uint32 Offset = Data.PropertyOffsets[i];
			FMemoryReader Ar(Data.PropertyData);
			Ar.Seek(Offset);
			SpudPropertyUtil::ReadRaw(OutValue, Ar);
			return true;
		}
		return false;
	}

	/// Safely set a variable-length value
	template <typename T>
    void SetVariableLength(const FString& Name, const T& Value)
	{
		const int i =  GetPropertyIndex(Name);
		if (i >= 0 && i < Data.PropertyNames.Num() - 1)
		{
			// This value already exists in the data structure, and it's not at the end
			// That means we might need to relocate data. The safest way to do this is to remove the existing
			// entry, and then Set() will re-add it at the end.
			// We *could* check whether the data has actually changed size before doing this, but that requires
			// doing a dummy write to a new buffer to correctly gauge the size anyway. Calling Set multiple times
			// for the same variable length value should be rare enough for the remove & re-add approach to be better
			Remove(i);
		}
		Set(Name, Value);
	}

	void Remove(int Index)
	{
		if (Index >= 0 && Index < Data.PropertyNames.Num())
		{
			if (Index == Data.PropertyNames.Num() - 1)
			{
				// Last entry, just truncate
				Data.PropertyData.SetNum(Data.PropertyOffsets[Index]);
			}
			else
			{
				// Remove from middle
				const int DataLen = Data.PropertyOffsets[Index+1] - Data.PropertyOffsets[Index];
				Data.PropertyData.RemoveAt(Index, DataLen);
			}
			Data.PropertyNames.RemoveAt(Index);
			Data.PropertyOffsets.RemoveAt(Index);
		}
	}

	
	
public:
	/// Clear any properties in this instance
	UFUNCTION(BlueprintCallable)
	void Reset() { Data.Reset(); }
	
	/// Set a vector
	UFUNCTION(BlueprintCallable)
	void SetVector(const FString& Name, const FVector& V) { Set(Name, V); }
	/**
	 * Get a vector
	 * @param OutVector The vector we read if successful
	 * @return True if the value was read successfully
	 */
	UFUNCTION(BlueprintCallable)
    bool GetVector(const FString& Name, FVector& OutVector) { return Get(Name, OutVector); }

	/// Set a string
	UFUNCTION(BlueprintCallable)
    void SetString(const FString& Name, const FString& S)
	{
		// FStrings are variable length
		SetVariableLength(Name, S);
	}
	/**
	* Get a string
	* @param Name The Name of the string
	* @param OutString The string we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool GetString(const FString& Name, FString& OutString) { return Get(Name, OutString); }

	/// Set text
	UFUNCTION(BlueprintCallable)
    void SetText(const FString& Name, const FText& S)
	{
		// FTexts are variable length
		SetVariableLength(Name, S);
	}
	/**
	* Get text
	* @param OutText The text we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool GetText(const FString& Name, FText& OutText) { return Get(Name, OutText); }

	/// Set an int
	UFUNCTION(BlueprintCallable)
    void SetInt(const FString& Name, int V) { Set(Name, V); }
	/**
	* Get an int
	* @param OutInt The int we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool GetInt(const FString& Name, int& OutInt) { return Get(Name, OutInt); }

	/// Set an int64
	UFUNCTION(BlueprintCallable)
    void SetInt64(const FString& Name, int64 V) { Set(Name, V); }
	/**
	* Get an int64
	* @param OutInt64 The int64 we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool GetInt64(const FString& Name, int64& OutInt64) { return Get(Name, OutInt64); }

	/// Set a float
	UFUNCTION(BlueprintCallable)
    void SetFloat(const FString& Name, float V) { Set(Name, V); }
	/**
	* Get a float
	* @param OutFloat The float we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool GetFloat(const FString& Name, float& OutFloat) { return Get(Name, OutFloat); }

	/// Set a byte
	UFUNCTION(BlueprintCallable)
    void SetByte(const FString& Name, uint8 V) { Set(Name, V); }
	/**
	* Get a byte
	* @param OutByte The byte we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool GetByte(const FString& Name, uint8& OutByte) { return Get(Name, OutByte); }

	/// Populate the internal data (copies)
	void SetData(const FSpudSaveCustomInfo& InData) { Data = InData; }
	/// Retrieve the internal data
	const FSpudSaveCustomInfo& GetData() const { return Data; }
};

