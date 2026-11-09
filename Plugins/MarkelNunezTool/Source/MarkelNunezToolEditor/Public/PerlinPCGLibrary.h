// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PerlinPCGLibrary.generated.h"

class APCGVolume;
class UStaticMesh;

UCLASS()
class MARKELNUNEZTOOLEDITOR_API UPerlinPCGLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static const TCHAR* const DefaultCubeTreePath;
	static const TCHAR* const DefaultSphereTreePath;

	// Places (or reuses) a PCG volume running PCG_PerlinCubes and regenerates it.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|PCG")
	static APCGVolume* SpawnPerlinPCG();

	// Copies PCG_PerlinCubes into PCG_PerlinTrees, swaps its cube and sphere meshes for the given trees,
	// and regenerates the PCG volume with it. Null meshes fall back to the default trees.
	// PositionJitter randomly offsets each tree up to that many units in X and Y so they don't form a perfect grid.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|PCG")
	static bool ReplaceShapesWithTrees(UStaticMesh* CubeTree, UStaticMesh* SphereTree, float PositionJitter = 70.0f);

	// Sets a numeric graph parameter (float, double or integer, e.g. "SphereThreshold") on the PerlinPCG volume
	// in the level. Only that volume's override changes; the graph assets keep their defaults.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|PCG")
	static bool SetPerlinPCGNumber(FName ParameterName, float Value, bool bRegenerate = true);

	// Sets a vector graph parameter (e.g. "GridExtents" or "CellSize") on the PerlinPCG volume in the level.
	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|PCG")
	static bool SetPerlinPCGVector(FName ParameterName, FVector Value, bool bRegenerate = true);

	UFUNCTION(BlueprintCallable, Category = "Markel Nunez Tools|PCG")
	static void RegeneratePerlinPCG();
};
