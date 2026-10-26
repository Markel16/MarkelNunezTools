// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PerlinLibrary.generated.h"
#include "PerlinLibrary.h"
#include "Engine/Texture2D.h"

class FMarkelNunezToolModule : public IModuleInterface
{
public:

	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	GENERATED_BODY()
	UFUNCTION(BlueprintPure, Category = "ABCTools|Perlin")
	static float GetPerlinValue(int32 X, int32 Y, float Scale, int32 Seed);
	sUFUNCTION(BlueprintCallable, Category = "ABCTools|Perlin")
	static UTexture2D* GeneratePerlinTexture(int32 Width = 256, int32 Height = 256,
	float Scale = 0.05f, int32 Seed = 0);

	float UPerlinLibrary::GetPerlinValue(int32 X, int32 Y, float Scale, int32 Seed)
{
	// The seed offsets the sample point: same seed → same texture
	const FVector2D P(X * Scale + Seed * 31.7f, Y * Scale + Seed * 17.3f);
	// PerlinNoise2D returns [-1, 1]; remap it to [0, 1]
	return FMath::Clamp((FMath::PerlinNoise2D(P) + 1.f) * 0.5f, 0.f, 1.f);
}
};
