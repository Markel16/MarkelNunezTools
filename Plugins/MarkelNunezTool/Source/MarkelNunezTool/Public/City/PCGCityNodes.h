// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "City/CityLayout.h"
#include "PCGSettings.h"
#include "PCGCityNodes.generated.h"

struct FCityParams;
struct FCityPiece;

// Pin labels shared by the nodes, the subgraphs and the graph builder.
namespace PCGCityPins
{
	MARKELNUNEZTOOL_API extern const FName Params;
	MARKELNUNEZTOOL_API extern const FName Crossings;
	MARKELNUNEZTOOL_API extern const FName Streets;
	MARKELNUNEZTOOL_API extern const FName Blocks;
	MARKELNUNEZTOOL_API extern const FName Lots;
	MARKELNUNEZTOOL_API extern const FName Built;
	MARKELNUNEZTOOL_API extern const FName Buildings;
	MARKELNUNEZTOOL_API extern const FName Lamps;
	MARKELNUNEZTOOL_API extern const FName Trees;
	MARKELNUNEZTOOL_API extern const FName Buildable;
	MARKELNUNEZTOOL_API extern const FName Parks;
	MARKELNUNEZTOOL_API extern const FName Lawns;
	MARKELNUNEZTOOL_API extern const FName ParkTrees;
	MARKELNUNEZTOOL_API extern const FName ParkBushes;
	MARKELNUNEZTOOL_API extern const FName Crosswalks;
	MARKELNUNEZTOOL_API extern const FName CrosswalkStripes;
	MARKELNUNEZTOOL_API extern const FName Entrances;
	MARKELNUNEZTOOL_API extern const FName Cars;
	MARKELNUNEZTOOL_API extern const FName Signs;
}

// Assets the car and sign points name in their Mesh and Material attributes. The graph builder creates the materials.
namespace PCGCityAssets
{
	MARKELNUNEZTOOL_API extern const TCHAR* const CityFolder;
	MARKELNUNEZTOOL_API extern const FName MeshAttribute;
	MARKELNUNEZTOOL_API extern const FName MaterialAttribute;

	MARKELNUNEZTOOL_API FString GetLookMaterialName(FName Look);
	MARKELNUNEZTOOL_API FSoftObjectPath GetShapeMeshPath(ECityShape Shape);
	MARKELNUNEZTOOL_API FSoftObjectPath GetMaterialPath(const FString& AssetName);
}

// Gathers the city parameters into one Attribute Set that travels to every layer on the Params pin.
// The five values the tool controls are overridable, so the main graph wires its Graph Parameters into them.
UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityParamsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityParams"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "ParamsTitle", "City Params"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "ParamsTooltip", "Packs the city parameters into an Attribute Set for the layer nodes."); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Param; }
#endif

	FCityParams ToCityParams() const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "City", meta = (PCG_Overridable, ClampMin = "100"))
	double CitySize = 40000.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "City", meta = (PCG_Overridable, ClampMin = "100"))
	double BlockSize = 6000.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "City", meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double BuildingDensity = 0.6;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "City", meta = (PCG_Overridable, ClampMin = "300"))
	double BuildingHeight = 3000.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "City", meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double BuildingVariation = 0.5;

	// Share of blocks that become parks instead of being built.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Parks", meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double ParkRatio = 0.15;

	// Chance of a tree in each park cell; cells without a tree may get a bush.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Parks", meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double ParkTreeDensity = 0.6;

	// 0 = every plant the same size, 1 = from half to one and a half times the base size.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Parks", meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double VegetationScaleVariation = 0.4;

	// Share of the legal curb slots that get a parked car.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Parking", meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double ParkingOccupancy = 0.7;

	// Minimum distance in cm from a parked car to any crossing or crosswalk.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Parking", meta = (PCG_Overridable, ClampMin = "0"))
	double ParkingClearance = 500.0;

	// 0 = every car the same model and color, 1 = all models and colors.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Parking", meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double CarVariation = 0.7;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Signs", meta = (PCG_Overridable))
	bool StopSigns = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Signs", meta = (PCG_Overridable))
	bool YieldSigns = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Signs", meta = (PCG_Overridable))
	bool DirectionSigns = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Signs", meta = (PCG_Overridable))
	bool SpeedLimitSigns = true;

	// Distance in cm from the edge of the intersection to the stop, yield and direction signs.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Signs", meta = (PCG_Overridable, ClampMin = "0"))
	double SignDistance = 400.0;

	// One speed limit sign every SignSpacing cm along each street segment; 0 places none.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Signs", meta = (PCG_Overridable, ClampMin = "0"))
	double SignSpacing = 3000.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Constants", meta = (PCG_Overridable, ClampMin = "1"))
	double StreetWidth = 800.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Constants", meta = (PCG_Overridable, ClampMin = "0"))
	double SidewalkWidth = 300.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Constants", meta = (PCG_Overridable, ClampMin = "100"))
	double LotSize = 2000.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Constants", meta = (PCG_Overridable, ClampMin = "100"))
	double PropSpacing = 1200.0;

	// Uniform scale applied to the tree mesh.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Constants", meta = (PCG_Overridable, ClampMin = "0.01"))
	double TreeScale = 0.5;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return {}; }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

// Base for the layer nodes: each one reads points from named input pins, runs one step of the
// algorithm in CityLayout.h and writes points with attributes to named output pins.
UCLASS(Abstract, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityLayerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

	// Makes the PCG component's Seed reach Context->GetSeed() and the cache key, so a new seed regenerates the city.
	virtual bool UseSeed() const override { return true; }

	// Runs the layer. Returns false if the inputs were invalid; errors are logged on the node.
	virtual bool RunLayer(struct FPCGContext* Context, const FCityParams& Params) const PURE_VIRTUAL(UPCGCityLayerSettings::RunLayer, return false;);

	// The grid depends on where the actor is, so it must not be served from the cache after the actor moves.
	virtual bool DependsOnActorTransform() const { return false; }

protected:
	virtual TArray<FName> GetInputLabels() const PURE_VIRTUAL(UPCGCityLayerSettings::GetInputLabels, return {};);
	virtual TArray<FName> GetOutputLabels() const PURE_VIRTUAL(UPCGCityLayerSettings::GetOutputLabels, return {};);

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityGridSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityGrid"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "GridTitle", "City Grid"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "GridTooltip", "Step 03: a grid of crossings centered on the actor, with Col and Row."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;
	virtual bool DependsOnActorTransform() const override { return true; }

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityStreetsSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityStreets"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "StreetsTitle", "City Streets"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "StreetsTooltip", "Step 04: one street segment between neighbouring crossings."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityBlocksSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityBlocks"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "BlocksTitle", "City Blocks"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "BlocksTooltip", "Step 05: a raised sidewalk block between the streets, with BlockID."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityLotsSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityLots"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "LotsTitle", "City Lots"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "LotsTooltip", "Step 06: a grid of lots inside each block, with Frontage on the street side."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityDensitySettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityDensity"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "DensityTitle", "City Density"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "DensityTooltip", "Step 07: keeps the frontage lots whose seeded value is below BuildingDensity."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityBuildingsSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityBuildings"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "BuildingsTitle", "City Buildings"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "BuildingsTooltip", "Step 08: one building per lot with seeded height, size and position, facing the street."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityPropsSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityProps"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "PropsTitle", "City Props"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "PropsTooltip", "Step 09: lamps and trees alternating along the sidewalks, away from crossings."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityOverlapsSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityCleanOverlaps"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "OverlapsTitle", "City Clean Overlaps"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "OverlapsTooltip", "Step 10: removes anything on the road or inside a building, then checks every layer."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityParksSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityParks"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "ParksTitle", "City Parks"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "ParksTooltip", "Parks: turns ParkRatio of the blocks into parks; the rest go on as buildable blocks."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityParkVegetationSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityParkVegetation"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "ParkVegetationTitle", "City Park Vegetation"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "ParkVegetationTooltip", "Parks: a lawn in each park and trees or bushes scattered over a grid of cells."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityCrosswalksSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityCrosswalks"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "CrosswalksTitle", "City Crosswalks"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "CrosswalksTooltip", "Parking: a crosswalk zone and its zebra stripes at each end of every street."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityEntrancesSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityEntrances"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "EntrancesTitle", "City Entrances"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "EntrancesTooltip", "Parking: a no-parking entrance in the curb lane in front of each building."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCityParkingSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CityParking"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "ParkingTitle", "City Parking"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "ParkingTooltip", "Parking: cars along the curb, aligned with the street, away from crossings, crosswalks and entrances."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MARKELNUNEZTOOL_API UPCGCitySignsSettings : public UPCGCityLayerSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CitySigns"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGCity", "SignsTitle", "City Signs"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("PCGCity", "SignsTooltip", "Signs: stop or yield before intersections, direction signs where main streets cross and speed limits along the streets, on the sidewalk facing the traffic."); }
#endif

	virtual bool RunLayer(FPCGContext* Context, const FCityParams& Params) const override;

protected:
	virtual TArray<FName> GetInputLabels() const override;
	virtual TArray<FName> GetOutputLabels() const override;
};
