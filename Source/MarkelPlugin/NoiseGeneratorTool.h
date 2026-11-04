// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoiseGeneratorTool.generated.h"

/**
 * 
 */
UCLASS()
class MARKELPLUGIN_API UNoiseGeneratorTool : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "PerlinNoise")
	static TArray<int32> PerlinNoise();
	
};
