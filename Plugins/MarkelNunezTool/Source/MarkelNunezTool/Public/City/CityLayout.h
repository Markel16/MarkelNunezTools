// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Pure C++ version of the city algorithm from the PCG guide. The PCG nodes in PCGCityNodes.h call these
// functions one layer at a time, and the automation tests call them directly. All distances are in cm.

struct MARKELNUNEZTOOL_API FCityParams
{
	// Values the tool sends.
	double CitySize = 40000.0;
	double BlockSize = 6000.0;
	double BuildingDensity = 0.6;
	double BuildingHeight = 3000.0;
	double BuildingVariation = 0.5;

	// Parks (improvement): share of blocks that become parks, chance of a tree in each park cell,
	// and how much the vegetation scale varies (0 = all the same size).
	double ParkRatio = 0.15;
	double ParkTreeDensity = 0.6;
	double VegetationScaleVariation = 0.4;

	// Internal constants.
	double StreetWidth = 800.0;
	double SidewalkWidth = 300.0;
	double LotSize = 2000.0;
	double PropSpacing = 1200.0;

	double RoadThickness = 10.0;
	double SidewalkHeight = 15.0;
	double MinBuildingHeight = 300.0;
	double LampHeight = 450.0;
	double LampRadius = 15.0;
	double TreeScale = 0.5;
	double TreeCrown = 300.0;

	// Park vegetation. Mesh sizes are in mesh units (PCG_Tree_02 is the widest park tree, PCG_Sapling_01 the bush).
	double LawnThickness = 5.0;
	double ParkCellSize = 1000.0;
	double ParkTreeScale = 0.6;
	double ParkTreeCrownLocal = 680.0;
	double ParkTreeHeightLocal = 1250.0;
	double ParkBushChance = 0.5;
	double ParkBushScale = 0.8;
	double ParkBushSizeLocal = 220.0;

	double Cell() const { return BlockSize + StreetWidth; }
	int32 CrossingsPerSide() const;
	bool IsValid(FString* OutError = nullptr) const;
};

// One city element, stored the way a PCG point stores it: a transform plus a box in mesh-local units.
struct MARKELNUNEZTOOL_API FCityPiece
{
	FVector Location = FVector::ZeroVector;
	double Yaw = 0.0;
	FVector Scale = FVector::OneVector;
	FBox LocalBounds = FBox(FVector(-50.0), FVector(50.0));
	int32 Seed = 0;
	float Density = 1.0f;

	int32 Col = INDEX_NONE;
	int32 Row = INDEX_NONE;
	int32 BlockID = INDEX_NONE;
	bool bFrontage = false;
	FVector BlockCenter = FVector::ZeroVector;
	double Height = 0.0;

	// Box sized for the engine's 100 cm basic shapes, which have their pivot at the center.
	static FCityPiece MakeBox(const FVector& Center, const FVector& Size, double Yaw = 0.0);

	FTransform GetTransform() const;
	FBox GetWorldBox() const;
	FBox2D GetFootprint() const;
};

struct MARKELNUNEZTOOL_API FCityLayout
{
	TArray<FCityPiece> Crossings;
	TArray<FCityPiece> Streets;
	TArray<FCityPiece> Blocks;
	TArray<FCityPiece> BuildableBlocks;
	TArray<FCityPiece> Parks;
	TArray<FCityPiece> Lawns;
	TArray<FCityPiece> ParkTrees;
	TArray<FCityPiece> ParkBushes;
	TArray<FCityPiece> Lots;
	TArray<FCityPiece> Built;
	TArray<FCityPiece> Buildings;
	TArray<FCityPiece> Lamps;
	TArray<FCityPiece> Trees;
};

struct MARKELNUNEZTOOL_API FCityOverlapReport
{
	int32 RemovedBuildings = 0;
	int32 RemovedLamps = 0;
	int32 RemovedTrees = 0;
	TArray<FString> FailedChecks;
};

namespace CityLayout
{
	// Stable per-point seed taken from the position, like $Seed in PCG.
	MARKELNUNEZTOOL_API int32 SeedFromPosition(const FVector& Position);

	// Step 03: a grid of crossings centered on Origin, with Col and Row.
	MARKELNUNEZTOOL_API TArray<FCityPiece> BuildCrossings(const FCityParams& Params, const FVector& Origin);

	// Step 04: one street segment between each pair of neighbouring crossings.
	MARKELNUNEZTOOL_API TArray<FCityPiece> BuildStreets(const FCityParams& Params, TConstArrayView<FCityPiece> Crossings);

	// Step 05: one raised sidewalk block in each cell between streets, with BlockID.
	MARKELNUNEZTOOL_API TArray<FCityPiece> BuildBlocks(const FCityParams& Params, TConstArrayView<FCityPiece> Crossings);

	// Parks: turns round(ParkRatio * blocks) blocks into parks, picked by a seeded value per block.
	// The rest are returned as buildable blocks for the lots.
	MARKELNUNEZTOOL_API void SplitParks(const FCityParams& Params, TConstArrayView<FCityPiece> Blocks, int32 Seed, TArray<FCityPiece>& OutBuildable, TArray<FCityPiece>& OutParks);

	// Parks: a lawn over the inside of each park and, in a grid of cells, one tree (chance ParkTreeDensity)
	// or one bush per cell, jittered inside its cell so they never overlap.
	MARKELNUNEZTOOL_API void BuildParkVegetation(const FCityParams& Params, TConstArrayView<FCityPiece> Parks, int32 Seed, TArray<FCityPiece>& OutLawns, TArray<FCityPiece>& OutTrees, TArray<FCityPiece>& OutBushes);

	// Step 06: a grid of lots inside each block, marking the ones that touch the street (Frontage).
	MARKELNUNEZTOOL_API TArray<FCityPiece> BuildLots(const FCityParams& Params, TConstArrayView<FCityPiece> Blocks);

	// Step 07: frontage lots whose seeded random value is below BuildingDensity.
	MARKELNUNEZTOOL_API TArray<FCityPiece> PickBuiltLots(const FCityParams& Params, TConstArrayView<FCityPiece> Lots, int32 Seed);

	// Step 08: one building per built lot with seeded height, footprint, offset and street-facing yaw.
	MARKELNUNEZTOOL_API TArray<FCityPiece> BuildBuildings(const FCityParams& Params, TConstArrayView<FCityPiece> BuiltLots, int32 Seed);

	// Step 09: lamps and trees alternating along the middle of each sidewalk, away from crossings.
	MARKELNUNEZTOOL_API void BuildProps(const FCityParams& Params, TConstArrayView<FCityPiece> Blocks, TConstArrayView<FCityPiece> Crossings, int32 Seed, TArray<FCityPiece>& OutLamps, TArray<FCityPiece>& OutTrees);

	// Step 10: removes buildings and props on the road, props inside buildings and overlapping buildings,
	// then checks every layer.
	MARKELNUNEZTOOL_API FCityOverlapReport CleanOverlaps(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TConstArrayView<FCityPiece> Crossings, TArray<FCityPiece>& InOutBuildings, TArray<FCityPiece>& InOutLamps, TArray<FCityPiece>& InOutTrees);

	// Runs every step in order. Used by the tests and the determinism check.
	MARKELNUNEZTOOL_API FCityLayout Generate(const FCityParams& Params, const FVector& Origin, int32 Seed, FCityOverlapReport* OutReport = nullptr);

	// True when both layouts place the same pieces in the same places.
	MARKELNUNEZTOOL_API bool AreEqual(const FCityLayout& A, const FCityLayout& B);
}
