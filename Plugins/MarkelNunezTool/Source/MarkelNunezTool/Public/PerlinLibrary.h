// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PerlinLibrary.generated.h"

UCLASS()
class MARKELNUNEZTOOL_API UPerlinLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Markel Nunez Tools|Perlin")
	static float GetPerlinValue(int32 X, int32 Y, float Scale, int32 Seed);

	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|Perlin")
	static UTexture2D* GeneratePerlinTexture(
		int32 Width = 256,
		int32 Height = 256,
		float Scale = 0.05f,
		int32 Seed = 0);
};
