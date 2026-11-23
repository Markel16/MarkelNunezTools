// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CityToolLibrary.generated.h"

// Step 11 of the PCG city guide: the tool the level designer uses to generate, clean up and test the city
// without opening the graph. Sizes and heights are in meters; the graph works in cm.
UCLASS()
class MARKELNUNEZTOOLEDITOR_API UCityToolLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Creates PCG_City, its SG_City* subgraphs and the city materials in /MarkelNunezTool/City.
	// Existing graphs are rebuilt in place so the level keeps its references to them.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|City")
	static bool BuildCityGraphs();

	// Places (or reuses) the City volume, sends the parameters to PCG_City and regenerates it.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|City")
	static bool GenerateCity(
		double CitySizeMeters = 400.0,
		double BlockSizeMeters = 60.0,
		double BuildingDensity = 0.6,
		double BuildingHeightMeters = 30.0,
		double BuildingVariation = 0.5,
		double ParkRatio = 0.15,
		double ParkTreeDensity = 0.6,
		double VegetationScaleVariation = 0.4,
		int32 Seed = 1234);

	// Removes everything PCG generated and every actor tagged CityGenerated, including the City volume.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|City")
	static void CleanupCity();

	// Runs the city algorithm with seed 7 twice and seed 8 once: the first two must match and the third must differ.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|City")
	static bool TestDeterminism(FString& Report,
		double CitySizeMeters = 400.0,
		double BlockSizeMeters = 60.0,
		double BuildingDensity = 0.6,
		double BuildingHeightMeters = 30.0,
		double BuildingVariation = 0.5,
		double ParkRatio = 0.15,
		double ParkTreeDensity = 0.6,
		double VegetationScaleVariation = 0.4);
};
