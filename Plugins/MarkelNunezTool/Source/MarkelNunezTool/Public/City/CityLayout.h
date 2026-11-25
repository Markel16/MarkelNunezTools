// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Pure C++ version of the city algorithm from the PCG guide. The PCG nodes in PCGCityNodes.h call these
// functions one layer at a time, and the automation tests call them directly. All distances are in cm.

// Engine basic shape a piece is drawn with when its mesh comes from an attribute (cars, signs).
enum class ECityShape : uint8
{
	Cube,
	Cylinder,
	Cone,
};

enum class ECitySignType : int32
{
	Stop,
	Yield,
	Direction,
	SpeedLimit,
};

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

	// Parked cars (improvement): share of free curb slots with a car, minimum distance (cm) from a car to
	// crossings and crosswalks, and how many models and colors are used (0 = all cars identical).
	double ParkingOccupancy = 0.7;
	double ParkingClearance = 500.0;
	double CarVariation = 0.7;

	// Traffic signs (improvement): which types are placed, how far before the intersection (cm),
	// and the spacing (cm) of speed limit signs along each street segment (0 = none).
	bool bStopSigns = true;
	bool bYieldSigns = true;
	bool bDirectionSigns = true;
	bool bSpeedLimitSigns = true;
	double SignDistance = 400.0;
	double SignSpacing = 3000.0;

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

	// Street markings and parking.
	double MarkingThickness = 2.0;
	double CrosswalkLength = 300.0;
	double CrosswalkStripePitch = 100.0;
	double CrosswalkStripeWidth = 50.0;
	double EntranceWidth = 400.0;
	double ParkingLaneWidth = 220.0;
	double CarSlotLength = 600.0;
	double CarLift = 20.0;
	double WheelSize = 60.0;
	double WheelWidth = 25.0;

	// Traffic signs. Every MajorStreetEvery-th row and column is a main street.
	int32 MajorStreetEvery = 3;
	double SignCurbOffset = 50.0;
	double SignPoleHeight = 230.0;
	double SignPoleRadius = 4.0;
	double SignPropClearance = 80.0;
	double DirectionSignSetback = 500.0;

	double Cell() const { return BlockSize + StreetWidth; }
	int32 CrossingsPerSide() const;
	bool IsValid(FString* OutError = nullptr) const;
};

// One city element, stored the way a PCG point stores it: a transform plus a box in mesh-local units.
struct MARKELNUNEZTOOL_API FCityPiece
{
	FVector Location = FVector::ZeroVector;
	double Yaw = 0.0;
	double Pitch = 0.0;
	double Roll = 0.0;
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

	// Parked cars: which model the piece belongs to and its color (INDEX_NONE for tires).
	int32 ModelIndex = INDEX_NONE;
	int32 ColorIndex = INDEX_NONE;

	// Pieces drawn from attributes (cars, signs): the shape and the material key, see CityLayout::GetLooks().
	ECityShape Shape = ECityShape::Cube;
	FName Look;
	int32 SignType = INDEX_NONE;

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
	TArray<FCityPiece> Crosswalks;
	TArray<FCityPiece> CrosswalkStripes;
	TArray<FCityPiece> Entrances;
	TArray<FCityPiece> Cars;
	TArray<FCityPiece> CarParts;
	TArray<FCityPiece> Signs;
	TArray<FCityPiece> SignParts;
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

	// Parking: a crosswalk zone at each end of every street segment, plus the zebra stripes to draw it.
	MARKELNUNEZTOOL_API void BuildCrosswalks(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TArray<FCityPiece>& OutZones, TArray<FCityPiece>& OutStripes);

	// Parking: one entrance in the curb lane in front of each building, on the side the building faces.
	MARKELNUNEZTOOL_API TArray<FCityPiece> BuildEntrances(const FCityParams& Params, TConstArrayView<FCityPiece> Buildings, TConstArrayView<FCityPiece> Blocks);

	// Parking: slots along the curb of every street, aligned with it, skipping any slot closer than
	// ParkingClearance to a crossing or crosswalk or touching an entrance; ParkingOccupancy of the rest get a car.
	// OutCars holds one body per car (for tests and overlap checks), OutParts the cabins and wheels.
	MARKELNUNEZTOOL_API void BuildParkedCars(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TConstArrayView<FCityPiece> Crossings, TConstArrayView<FCityPiece> Blocks, TConstArrayView<FCityPiece> Crosswalks, TConstArrayView<FCityPiece> Entrances, int32 Seed, TArray<FCityPiece>& OutCars, TArray<FCityPiece>& OutParts);

	// Car body colors, indexed by FCityPiece::ColorIndex.
	MARKELNUNEZTOOL_API TConstArrayView<FLinearColor> GetCarPalette();
	MARKELNUNEZTOOL_API int32 GetCarModelCount();

	// Every material key a piece's Look can name, with its color. The graph builder makes one material per entry.
	MARKELNUNEZTOOL_API TConstArrayView<TPair<FName, FLinearColor>> GetLooks();

	// Main streets get priority at intersections; rows and columns are those of the crossings grid.
	MARKELNUNEZTOOL_API bool IsMajorStreet(const FCityParams& Params, int32 RowOrCol);

	// Traffic signs on the sidewalk, on the driver's right, facing the traffic they address:
	// stop or yield before intersections depending on which street has priority, direction signs where two
	// main streets cross, and speed limits every SignSpacing along each segment. Signs move along the curb
	// to stay clear of lamps and trees. OutSigns holds one pole per sign (with SignType), OutParts the panels.
	MARKELNUNEZTOOL_API void BuildSigns(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TConstArrayView<FCityPiece> Crossings, TConstArrayView<FCityPiece> Blocks, TConstArrayView<FCityPiece> Lamps, TConstArrayView<FCityPiece> Trees, TArray<FCityPiece>& OutSigns, TArray<FCityPiece>& OutParts);

	// Runs every step in order. Used by the tests and the determinism check.
	MARKELNUNEZTOOL_API FCityLayout Generate(const FCityParams& Params, const FVector& Origin, int32 Seed, FCityOverlapReport* OutReport = nullptr);

	// True when both layouts place the same pieces in the same places.
	MARKELNUNEZTOOL_API bool AreEqual(const FCityLayout& A, const FCityLayout& B);
}
