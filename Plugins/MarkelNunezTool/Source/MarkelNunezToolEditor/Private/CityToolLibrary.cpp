// Copyright Epic Games, Inc. All Rights Reserved.

#include "CityToolLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "City/CityLayout.h"
#include "City/PCGCityNodes.h"
#include "Editor.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGUserParameterGet.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Helpers/PCGGraphParametersHelpers.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Misc/PackageName.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSubgraph.h"
#include "PCGVolume.h"
#include "ScopedTransaction.h"
#include "StructUtils/PropertyBag.h"

namespace CityTool
{
	const FName CityTag(TEXT("City"));
	const FName CityGeneratedTag(TEXT("CityGenerated"));
	const TCHAR* const CityFolder = TEXT("/MarkelNunezTool/City");
	const TCHAR* const MainGraphName = TEXT("PCG_City");

	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const TreeMeshPath = TEXT("/PCGBiomeSample/Meshes/PCG_Tree_01.PCG_Tree_01");
	const TCHAR* const ParkTreeMeshPaths[] = {
		TEXT("/PCGBiomeSample/Meshes/PCG_Tree_01.PCG_Tree_01"),
		TEXT("/PCGBiomeSample/Meshes/PCG_Tree_02.PCG_Tree_02"),
	};
	const TCHAR* const BushMeshPath = TEXT("/PCGBiomeSample/Meshes/PCG_Sapling_01.PCG_Sapling_01");
	const TCHAR* const BaseMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const FName MaterialColorParameter(TEXT("Color"));

	// The default PCG volume brush is 200 units wide.
	constexpr double VolumeBrushSize = 200.0;
	constexpr double MetersToCm = 100.0;

	// The six values the tool sends, as Graph Parameters of PCG_City (cm).
	struct FGraphParameter
	{
		FName Name;
		double Default;
		bool bIsBool = false;
	};
	const FGraphParameter GraphParameters[] = {
		{ TEXT("CitySize"), 40000.0 },
		{ TEXT("BlockSize"), 6000.0 },
		{ TEXT("BuildingDensity"), 0.6 },
		{ TEXT("BuildingHeight"), 3000.0 },
		{ TEXT("BuildingVariation"), 0.5 },
		{ TEXT("ParkRatio"), 0.15 },
		{ TEXT("ParkTreeDensity"), 0.6 },
		{ TEXT("VegetationScaleVariation"), 0.4 },
		{ TEXT("ParkingOccupancy"), 0.7 },
		{ TEXT("ParkingClearance"), 500.0 },
		{ TEXT("CarVariation"), 0.7 },
		{ TEXT("StopSigns"), 1.0, true },
		{ TEXT("YieldSigns"), 1.0, true },
		{ TEXT("DirectionSigns"), 1.0, true },
		{ TEXT("SpeedLimitSigns"), 1.0, true },
		{ TEXT("SignDistance"), 400.0 },
		{ TEXT("SignSpacing"), 3000.0 },
	};

	UWorld* GetEditorWorld()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			UE_LOG(LogTemp, Error, TEXT("There is no active editor world for the city."));
		}
		return World;
	}

	FString GetObjectPath(const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), CityFolder, *AssetName, *AssetName);
	}

	template <typename T>
	T* LoadCityAsset(const FString& AssetName)
	{
		const FString PackageName = FString::Printf(TEXT("%s/%s"), CityFolder, *AssetName);
		return FPackageName::DoesPackageExist(PackageName) ? LoadObject<T>(nullptr, *GetObjectPath(AssetName)) : nullptr;
	}

	// Loads the asset if it exists, otherwise creates it in a new package. Either way the package is collected for saving.
	template <typename T>
	T* LoadOrCreateCityAsset(const FString& AssetName, TArray<UPackage*>& OutPackages)
	{
		T* Asset = LoadCityAsset<T>(AssetName);
		if (!Asset)
		{
			UPackage* Package = CreatePackage(*FString::Printf(TEXT("%s/%s"), CityFolder, *AssetName));
			Asset = NewObject<T>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Asset);
		}

		Asset->Modify();
		Asset->MarkPackageDirty();
		OutPackages.AddUnique(Asset->GetPackage());
		return Asset;
	}

	UMaterialInterface* BuildMaterial(const FString& AssetName, const FLinearColor& Color, TArray<UPackage*>& OutPackages)
	{
		UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, BaseMaterialPath);
		if (!BaseMaterial)
		{
			return nullptr;
		}

		UMaterialInstanceConstant* Material = LoadOrCreateCityAsset<UMaterialInstanceConstant>(AssetName, OutPackages);
		Material->SetParentEditorOnly(BaseMaterial);
		Material->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(MaterialColorParameter), Color);
		Material->PostEditChange();
		return Material;
	}

	// Adds nodes and edges to one graph and reports any pin that does not exist instead of failing silently.
	class FGraphBuilder
	{
	public:
		explicit FGraphBuilder(UPCGGraph* InGraph) : Graph(InGraph) {}

		template <typename T>
		UPCGNode* Add(T*& OutSettings, const int32 X, const int32 Y)
		{
			UPCGNode* Node = Graph->AddNodeOfType<T>(OutSettings);
			Node->SetNodePosition(X, Y);
			return Node;
		}

		UPCGNode* AddSubgraph(UPCGGraph* Subgraph, const int32 X, const int32 Y)
		{
			UPCGSubgraphSettings* Settings = nullptr;
			UPCGNode* Node = Add(Settings, X, Y);
			Settings->SetSubgraph(Subgraph);
			Node->UpdateAfterSettingsChangeDuringCreation();
			return Node;
		}

		void Connect(UPCGNode* From, const FName FromPin, UPCGNode* To, const FName ToPin)
		{
			if (!From->GetOutputPin(FromPin) || !To->GetInputPin(ToPin))
			{
				UE_LOG(LogTemp, Error, TEXT("%s: cannot connect %s.%s to %s.%s, the pin does not exist."), *Graph->GetName(),
					*From->GetNodeTitle(EPCGNodeTitleType::ListView).ToString(), *FromPin.ToString(),
					*To->GetNodeTitle(EPCGNodeTitleType::ListView).ToString(), *ToPin.ToString());
				bSucceeded = false;
				return;
			}
			Graph->AddEdge(From, FromPin, To, ToPin);
		}

		bool Succeeded() const { return bSucceeded; }

	private:
		UPCGGraph* Graph;
		bool bSucceeded = true;
	};

	FPCGPinProperties MakeGraphPin(const FName Label)
	{
		const bool bIsParams = Label == PCGCityPins::Params;
		return FPCGPinProperties(Label, FPCGDataTypeIdentifier{ bIsParams ? EPCGDataType::Param : EPCGDataType::Point }, /*bAllowMultipleConnections=*/!bIsParams);
	}

	// Replaces the custom pins of a graph's Input or Output node.
	void SetGraphPins(UPCGNode* InputOutputNode, TConstArrayView<FName> Labels)
	{
		UPCGGraphInputOutputSettings* Settings = CastChecked<UPCGGraphInputOutputSettings>(InputOutputNode->GetSettings());
		Settings->Modify();

		// Pins is protected; reset it through reflection so a rebuild doesn't append "Params1", "Params2"...
		if (const FArrayProperty* PinsProperty = FindFProperty<FArrayProperty>(UPCGGraphInputOutputSettings::StaticClass(), TEXT("Pins")))
		{
			PinsProperty->ContainerPtrToValuePtr<TArray<FPCGPinProperties>>(Settings)->Reset();
		}

		for (const FName Label : Labels)
		{
			(void)Settings->AddPin(MakeGraphPin(Label));
		}
		InputOutputNode->UpdateAfterSettingsChangeDuringCreation();
	}

	// Empties a graph that is about to be rebuilt, keeping the asset so existing references stay valid.
	void ClearGraph(UPCGGraph* Graph)
	{
		TArray<UPCGNode*> Nodes = Graph->GetNodes();
		Graph->RemoveNodes(Nodes);
		for (UPCGNode* InputOutputNode : { Graph->GetInputNode(), Graph->GetOutputNode() })
		{
			for (UPCGPin* Pin : InputOutputNode->GetInputPins())
			{
				Pin->BreakAllEdges();
			}
			for (UPCGPin* Pin : InputOutputNode->GetOutputPins())
			{
				Pin->BreakAllEdges();
			}
		}
	}

	// A subgraph with one city node inside: Input -> node -> Output, with one graph pin per node pin.
	template <typename SettingsType>
	UPCGGraph* BuildLayerSubgraph(const FString& AssetName, TArray<UPackage*>& OutPackages, bool& bInOutSucceeded)
	{
		UPCGGraph* Graph = LoadOrCreateCityAsset<UPCGGraph>(AssetName, OutPackages);
		ClearGraph(Graph);

		FGraphBuilder Builder(Graph);
		SettingsType* Settings = nullptr;
		UPCGNode* Node = Builder.Add(Settings, 0, 0);

		// Skip the execution dependency pin every node has; only the city data pins go through the subgraph.
		const TSet<FName> CityLabels = {
			PCGCityPins::Params, PCGCityPins::Crossings, PCGCityPins::Streets, PCGCityPins::Blocks, PCGCityPins::Lots,
			PCGCityPins::Built, PCGCityPins::Buildings, PCGCityPins::Lamps, PCGCityPins::Trees, PCGCityPins::Buildable,
			PCGCityPins::Parks, PCGCityPins::Lawns, PCGCityPins::ParkTrees, PCGCityPins::ParkBushes,
			PCGCityPins::Crosswalks, PCGCityPins::CrosswalkStripes, PCGCityPins::Entrances, PCGCityPins::Cars, PCGCityPins::Signs };

		TArray<FName> InputLabels;
		for (const UPCGPin* Pin : Node->GetInputPins())
		{
			if (CityLabels.Contains(Pin->Properties.Label))
			{
				InputLabels.Add(Pin->Properties.Label);
			}
		}

		TArray<FName> OutputLabels;
		for (const UPCGPin* Pin : Node->GetOutputPins())
		{
			OutputLabels.Add(Pin->Properties.Label);
		}

		SetGraphPins(Graph->GetInputNode(), InputLabels);
		SetGraphPins(Graph->GetOutputNode(), OutputLabels);
		Graph->GetInputNode()->SetNodePosition(-400, 0);
		Graph->GetOutputNode()->SetNodePosition(400, 0);

		for (const FName Label : InputLabels)
		{
			Builder.Connect(Graph->GetInputNode(), Label, Node, Label);
		}
		for (const FName Label : OutputLabels)
		{
			Builder.Connect(Node, Label, Graph->GetOutputNode(), Label);
		}

		bInOutSucceeded &= Builder.Succeeded();
		return Graph;
	}

	// The weighted selector picks one of the meshes per point from the point's seed, so the choice is deterministic.
	UPCGNode* AddMeshSpawner(FGraphBuilder& Builder, TConstArrayView<const TCHAR*> MeshPaths, UMaterialInterface* Material, const int32 X, const int32 Y)
	{
		UPCGStaticMeshSpawnerSettings* Settings = nullptr;
		UPCGNode* Node = Builder.Add(Settings, X, Y);
		if (UPCGMeshSelectorWeighted* Selector = Cast<UPCGMeshSelectorWeighted>(Settings->MeshSelectorParameters))
		{
			for (const TCHAR* MeshPath : MeshPaths)
			{
				FPCGMeshSelectorWeightedEntry& Entry = Selector->MeshEntries.Emplace_GetRef(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(MeshPath)), 1);
				if (Material)
				{
					Entry.Descriptor.OverrideMaterials.Add(Material);
				}
			}
		}
		return Node;
	}

	UPCGNode* AddMeshSpawner(FGraphBuilder& Builder, const TCHAR* MeshPath, UMaterialInterface* Material, const int32 X, const int32 Y)
	{
		return AddMeshSpawner(Builder, MakeArrayView(&MeshPath, 1), Material, X, Y);
	}

	// Spawns whatever mesh and material each point names in its Mesh and Material attributes (the cars).
	UPCGNode* AddAttributeMeshSpawner(FGraphBuilder& Builder, const int32 X, const int32 Y)
	{
		UPCGStaticMeshSpawnerSettings* Settings = nullptr;
		UPCGNode* Node = Builder.Add(Settings, X, Y);
		Settings->SetMeshSelectorType(UPCGMeshSelectorByAttribute::StaticClass());
		if (UPCGMeshSelectorByAttribute* Selector = Cast<UPCGMeshSelectorByAttribute>(Settings->MeshSelectorParameters))
		{
			Selector->AttributeName = PCGCityAssets::MeshAttribute;
			Selector->bUseAttributeMaterialOverrides = true;
			Selector->MaterialOverrideAttributes = { PCGCityAssets::MaterialAttribute };
		}
		return Node;
	}

	// Datos primero, mallas al final: the only subgraph that turns points into meshes.
	UPCGGraph* BuildSpawnSubgraph(TArray<UPackage*>& OutPackages, bool& bInOutSucceeded)
	{
		UMaterialInterface* RoadMaterial = BuildMaterial(TEXT("MI_CityRoad"), FLinearColor(0.02f, 0.02f, 0.025f), OutPackages);
		UMaterialInterface* SidewalkMaterial = BuildMaterial(TEXT("MI_CitySidewalk"), FLinearColor(0.35f, 0.35f, 0.33f), OutPackages);
		UMaterialInterface* BuildingMaterial = BuildMaterial(TEXT("MI_CityBuilding"), FLinearColor(0.55f, 0.42f, 0.3f), OutPackages);
		UMaterialInterface* LampMaterial = BuildMaterial(TEXT("MI_CityLamp"), FLinearColor(0.05f, 0.05f, 0.05f), OutPackages);
		UMaterialInterface* GrassMaterial = BuildMaterial(TEXT("MI_CityGrass"), FLinearColor(0.08f, 0.25f, 0.05f), OutPackages);
		UMaterialInterface* CrosswalkMaterial = BuildMaterial(TEXT("MI_CityCrosswalk"), FLinearColor(0.9f, 0.9f, 0.9f), OutPackages);
		UMaterialInterface* NoParkingMaterial = BuildMaterial(TEXT("MI_CityNoParking"), FLinearColor(0.9f, 0.65f, 0.02f), OutPackages);

		// The car points name these materials in their Material attribute.
		for (const TPair<FName, FLinearColor>& Look : CityLayout::GetLooks())
		{
			BuildMaterial(PCGCityAssets::GetLookMaterialName(Look.Key), Look.Value, OutPackages);
		}

		UPCGGraph* Graph = LoadOrCreateCityAsset<UPCGGraph>(TEXT("SG_CitySpawn"), OutPackages);
		ClearGraph(Graph);

		const TArray<FName> InputLabels = {
			PCGCityPins::Streets, PCGCityPins::Crossings, PCGCityPins::Blocks, PCGCityPins::Buildings, PCGCityPins::Lamps, PCGCityPins::Trees,
			PCGCityPins::Lawns, PCGCityPins::ParkTrees, PCGCityPins::ParkBushes, PCGCityPins::CrosswalkStripes, PCGCityPins::Entrances,
			PCGCityPins::Cars, PCGCityPins::Signs };
		SetGraphPins(Graph->GetInputNode(), InputLabels);
		SetGraphPins(Graph->GetOutputNode(), {});
		Graph->GetInputNode()->SetNodePosition(-400, 0);
		Graph->GetOutputNode()->SetNodePosition(800, 0);

		FGraphBuilder Builder(Graph);
		UPCGNode* Input = Graph->GetInputNode();
		const FName In = PCGPinConstants::DefaultInputLabel;

		UPCGNode* Roads = AddMeshSpawner(Builder, CubeMeshPath, RoadMaterial, 200, -300);
		Builder.Connect(Input, PCGCityPins::Streets, Roads, In);
		Builder.Connect(Input, PCGCityPins::Crossings, Roads, In);

		Builder.Connect(Input, PCGCityPins::Blocks, AddMeshSpawner(Builder, CubeMeshPath, SidewalkMaterial, 200, -100), In);
		Builder.Connect(Input, PCGCityPins::Buildings, AddMeshSpawner(Builder, CubeMeshPath, BuildingMaterial, 200, 100), In);
		Builder.Connect(Input, PCGCityPins::Lamps, AddMeshSpawner(Builder, CylinderMeshPath, LampMaterial, 200, 300), In);
		Builder.Connect(Input, PCGCityPins::Trees, AddMeshSpawner(Builder, TreeMeshPath, nullptr, 200, 500), In);
		Builder.Connect(Input, PCGCityPins::Lawns, AddMeshSpawner(Builder, CubeMeshPath, GrassMaterial, 200, 700), In);
		Builder.Connect(Input, PCGCityPins::ParkTrees, AddMeshSpawner(Builder, ParkTreeMeshPaths, nullptr, 200, 900), In);
		Builder.Connect(Input, PCGCityPins::ParkBushes, AddMeshSpawner(Builder, BushMeshPath, nullptr, 200, 1100), In);
		Builder.Connect(Input, PCGCityPins::CrosswalkStripes, AddMeshSpawner(Builder, CubeMeshPath, CrosswalkMaterial, 200, 1300), In);
		Builder.Connect(Input, PCGCityPins::Entrances, AddMeshSpawner(Builder, CubeMeshPath, NoParkingMaterial, 200, 1500), In);
		Builder.Connect(Input, PCGCityPins::Cars, AddAttributeMeshSpawner(Builder, 200, 1700), In);
		Builder.Connect(Input, PCGCityPins::Signs, AddAttributeMeshSpawner(Builder, 200, 1900), In);

		bInOutSucceeded &= Builder.Succeeded();
		return Graph;
	}

	void AddMissingGraphParameters(UPCGGraph* Graph)
	{
		TArray<FPropertyBagPropertyDesc> Missing;
		const FInstancedPropertyBag* Existing = Graph->GetUserParametersStruct();
		for (const FGraphParameter& Parameter : GraphParameters)
		{
			if (!Existing || !Existing->FindPropertyDescByName(Parameter.Name))
			{
				Missing.Emplace(Parameter.Name, Parameter.bIsBool ? EPropertyBagPropertyType::Bool : EPropertyBagPropertyType::Double);
			}
		}

		if (!Missing.IsEmpty())
		{
			Graph->AddUserParameters(Missing);
			for (const FGraphParameter& Parameter : GraphParameters)
			{
				if (Parameter.bIsBool)
				{
					UPCGGraphParametersHelpers::SetBoolParameter(Graph, Parameter.Name, Parameter.Default != 0.0);
				}
				else
				{
					UPCGGraphParametersHelpers::SetDoubleParameter(Graph, Parameter.Name, Parameter.Default);
				}
			}
		}
	}

	APCGVolume* FindCityVolume(UWorld* World)
	{
		for (TActorIterator<APCGVolume> It(World); It; ++It)
		{
			if (It->ActorHasTag(CityTag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	FCityParams MakeParams(const double CitySizeMeters, const double BlockSizeMeters, const double BuildingDensity, const double BuildingHeightMeters, const double BuildingVariation, const double ParkRatio, const double ParkTreeDensity, const double VegetationScaleVariation, const double ParkingOccupancy, const double ParkingClearanceMeters, const double CarVariation, const bool bStopSigns, const bool bYieldSigns, const bool bDirectionSigns, const bool bSpeedLimitSigns, const double SignDistanceMeters, const double SignSpacingMeters)
	{
		FCityParams Params;
		Params.CitySize = CitySizeMeters * MetersToCm;
		Params.BlockSize = BlockSizeMeters * MetersToCm;
		Params.BuildingDensity = BuildingDensity;
		Params.BuildingHeight = BuildingHeightMeters * MetersToCm;
		Params.BuildingVariation = BuildingVariation;
		Params.ParkRatio = ParkRatio;
		Params.ParkTreeDensity = ParkTreeDensity;
		Params.VegetationScaleVariation = VegetationScaleVariation;
		Params.ParkingOccupancy = ParkingOccupancy;
		Params.ParkingClearance = ParkingClearanceMeters * MetersToCm;
		Params.CarVariation = CarVariation;
		Params.bStopSigns = bStopSigns;
		Params.bYieldSigns = bYieldSigns;
		Params.bDirectionSigns = bDirectionSigns;
		Params.bSpeedLimitSigns = bSpeedLimitSigns;
		Params.SignDistance = SignDistanceMeters * MetersToCm;
		Params.SignSpacing = SignSpacingMeters * MetersToCm;
		return Params;
	}
}

bool UCityToolLibrary::BuildCityGraphs()
{
	using namespace CityTool;

	const FScopedTransaction Transaction(NSLOCTEXT("MarkelNunezTool", "BuildCityGraphsTransaction", "Build city PCG graphs"));
	TArray<UPackage*> Packages;
	bool bSucceeded = true;

	UPCGGraph* GridGraph = BuildLayerSubgraph<UPCGCityGridSettings>(TEXT("SG_CityGrid"), Packages, bSucceeded);
	UPCGGraph* StreetsGraph = BuildLayerSubgraph<UPCGCityStreetsSettings>(TEXT("SG_CityStreets"), Packages, bSucceeded);
	UPCGGraph* BlocksGraph = BuildLayerSubgraph<UPCGCityBlocksSettings>(TEXT("SG_CityBlocks"), Packages, bSucceeded);
	UPCGGraph* ParksGraph = BuildLayerSubgraph<UPCGCityParksSettings>(TEXT("SG_CityParks"), Packages, bSucceeded);
	UPCGGraph* ParkVegetationGraph = BuildLayerSubgraph<UPCGCityParkVegetationSettings>(TEXT("SG_CityParkVegetation"), Packages, bSucceeded);
	UPCGGraph* LotsGraph = BuildLayerSubgraph<UPCGCityLotsSettings>(TEXT("SG_CityLots"), Packages, bSucceeded);
	UPCGGraph* DensityGraph = BuildLayerSubgraph<UPCGCityDensitySettings>(TEXT("SG_CityDensity"), Packages, bSucceeded);
	UPCGGraph* BuildingsGraph = BuildLayerSubgraph<UPCGCityBuildingsSettings>(TEXT("SG_CityBuildings"), Packages, bSucceeded);
	UPCGGraph* PropsGraph = BuildLayerSubgraph<UPCGCityPropsSettings>(TEXT("SG_CityProps"), Packages, bSucceeded);
	UPCGGraph* OverlapsGraph = BuildLayerSubgraph<UPCGCityOverlapsSettings>(TEXT("SG_CityOverlaps"), Packages, bSucceeded);
	UPCGGraph* CrosswalksGraph = BuildLayerSubgraph<UPCGCityCrosswalksSettings>(TEXT("SG_CityCrosswalks"), Packages, bSucceeded);
	UPCGGraph* EntrancesGraph = BuildLayerSubgraph<UPCGCityEntrancesSettings>(TEXT("SG_CityEntrances"), Packages, bSucceeded);
	UPCGGraph* ParkingGraph = BuildLayerSubgraph<UPCGCityParkingSettings>(TEXT("SG_CityParking"), Packages, bSucceeded);
	UPCGGraph* SignsGraph = BuildLayerSubgraph<UPCGCitySignsSettings>(TEXT("SG_CitySigns"), Packages, bSucceeded);
	UPCGGraph* SpawnGraph = BuildSpawnSubgraph(Packages, bSucceeded);

	// Main graph: it only wires the systems together.
	UPCGGraph* Main = LoadOrCreateCityAsset<UPCGGraph>(MainGraphName, Packages);
	ClearGraph(Main);
	AddMissingGraphParameters(Main);
	SetGraphPins(Main->GetInputNode(), {});
	SetGraphPins(Main->GetOutputNode(), {});
	Main->GetInputNode()->SetNodePosition(-800, -400);
	Main->GetOutputNode()->SetNodePosition(3200, -400);

	FGraphBuilder Builder(Main);
	UPCGCityParamsSettings* ParamsSettings = nullptr;
	UPCGNode* Params = Builder.Add(ParamsSettings, -300, 0);

	int32 ParameterY = -200;
	for (const FGraphParameter& Parameter : GraphParameters)
	{
		const FPropertyBagPropertyDesc* Desc = Main->GetUserParametersStruct()->FindPropertyDescByName(Parameter.Name);
		UPCGUserParameterGetSettings* GetSettings = nullptr;
		UPCGNode* GetNode = Builder.Add(GetSettings, -700, ParameterY);
		GetSettings->PropertyGuid = Desc ? Desc->ID : FGuid();
		GetSettings->PropertyName = Parameter.Name;
		GetNode->UpdateAfterSettingsChangeDuringCreation();
		Builder.Connect(GetNode, Parameter.Name, Params, Parameter.Name);
		ParameterY += 100;
	}

	UPCGNode* Grid = Builder.AddSubgraph(GridGraph, 100, -300);
	UPCGNode* Streets = Builder.AddSubgraph(StreetsGraph, 500, -400);
	UPCGNode* Blocks = Builder.AddSubgraph(BlocksGraph, 500, -100);
	UPCGNode* Parks = Builder.AddSubgraph(ParksGraph, 900, -100);
	UPCGNode* ParkVegetation = Builder.AddSubgraph(ParkVegetationGraph, 1300, 550);
	UPCGNode* Lots = Builder.AddSubgraph(LotsGraph, 1300, -100);
	UPCGNode* Density = Builder.AddSubgraph(DensityGraph, 1700, -100);
	UPCGNode* Buildings = Builder.AddSubgraph(BuildingsGraph, 2100, -100);
	UPCGNode* Props = Builder.AddSubgraph(PropsGraph, 1300, 250);
	UPCGNode* Overlaps = Builder.AddSubgraph(OverlapsGraph, 2500, -200);
	UPCGNode* Crosswalks = Builder.AddSubgraph(CrosswalksGraph, 900, -500);
	UPCGNode* Entrances = Builder.AddSubgraph(EntrancesGraph, 2900, 100);
	UPCGNode* Parking = Builder.AddSubgraph(ParkingGraph, 3300, 100);
	UPCGNode* Signs = Builder.AddSubgraph(SignsGraph, 3300, 450);
	UPCGNode* Spawn = Builder.AddSubgraph(SpawnGraph, 3800, -200);

	for (UPCGNode* Layer : { Grid, Streets, Blocks, Parks, ParkVegetation, Lots, Density, Buildings, Props, Overlaps, Crosswalks, Entrances, Parking, Signs })
	{
		Builder.Connect(Params, PCGCityPins::Params, Layer, PCGCityPins::Params);
	}

	for (UPCGNode* Consumer : { Streets, Blocks, Props, Overlaps, Parking, Signs, Spawn })
	{
		Builder.Connect(Grid, PCGCityPins::Crossings, Consumer, PCGCityPins::Crossings);
	}
	Builder.Connect(Streets, PCGCityPins::Streets, Overlaps, PCGCityPins::Streets);
	Builder.Connect(Streets, PCGCityPins::Streets, Spawn, PCGCityPins::Streets);
	// Parks take some blocks out of the building pipeline; every block keeps its sidewalk and street props.
	Builder.Connect(Blocks, PCGCityPins::Blocks, Parks, PCGCityPins::Blocks);
	Builder.Connect(Parks, PCGCityPins::Buildable, Lots, PCGCityPins::Blocks);
	Builder.Connect(Parks, PCGCityPins::Parks, ParkVegetation, PCGCityPins::Parks);
	for (const FName Label : { PCGCityPins::Lawns, PCGCityPins::ParkTrees, PCGCityPins::ParkBushes })
	{
		Builder.Connect(ParkVegetation, Label, Spawn, Label);
	}
	Builder.Connect(Blocks, PCGCityPins::Blocks, Props, PCGCityPins::Blocks);
	Builder.Connect(Blocks, PCGCityPins::Blocks, Spawn, PCGCityPins::Blocks);
	Builder.Connect(Lots, PCGCityPins::Lots, Density, PCGCityPins::Lots);
	Builder.Connect(Density, PCGCityPins::Built, Buildings, PCGCityPins::Built);
	Builder.Connect(Buildings, PCGCityPins::Buildings, Overlaps, PCGCityPins::Buildings);
	Builder.Connect(Props, PCGCityPins::Lamps, Overlaps, PCGCityPins::Lamps);
	Builder.Connect(Props, PCGCityPins::Trees, Overlaps, PCGCityPins::Trees);
	for (const FName Label : { PCGCityPins::Buildings, PCGCityPins::Lamps, PCGCityPins::Trees })
	{
		Builder.Connect(Overlaps, Label, Spawn, Label);
	}

	// Parked cars: crosswalks and entrances are the forbidden zones the parking layer avoids.
	Builder.Connect(Streets, PCGCityPins::Streets, Crosswalks, PCGCityPins::Streets);
	Builder.Connect(Overlaps, PCGCityPins::Buildings, Entrances, PCGCityPins::Buildings);
	Builder.Connect(Blocks, PCGCityPins::Blocks, Entrances, PCGCityPins::Blocks);
	Builder.Connect(Streets, PCGCityPins::Streets, Parking, PCGCityPins::Streets);
	Builder.Connect(Blocks, PCGCityPins::Blocks, Parking, PCGCityPins::Blocks);
	Builder.Connect(Crosswalks, PCGCityPins::Crosswalks, Parking, PCGCityPins::Crosswalks);
	Builder.Connect(Entrances, PCGCityPins::Entrances, Parking, PCGCityPins::Entrances);
	Builder.Connect(Crosswalks, PCGCityPins::CrosswalkStripes, Spawn, PCGCityPins::CrosswalkStripes);
	Builder.Connect(Entrances, PCGCityPins::Entrances, Spawn, PCGCityPins::Entrances);
	Builder.Connect(Parking, PCGCityPins::Cars, Spawn, PCGCityPins::Cars);

	// Traffic signs: on the sidewalks, clear of the final lamps and trees.
	Builder.Connect(Streets, PCGCityPins::Streets, Signs, PCGCityPins::Streets);
	Builder.Connect(Blocks, PCGCityPins::Blocks, Signs, PCGCityPins::Blocks);
	Builder.Connect(Overlaps, PCGCityPins::Lamps, Signs, PCGCityPins::Lamps);
	Builder.Connect(Overlaps, PCGCityPins::Trees, Signs, PCGCityPins::Trees);
	Builder.Connect(Signs, PCGCityPins::Signs, Spawn, PCGCityPins::Signs);

	bSucceeded &= Builder.Succeeded();
	UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty=*/false);

	UE_LOG(LogTemp, Display, TEXT("City graphs %s in %s (%d assets)."), bSucceeded ? TEXT("built") : TEXT("built with errors"), CityFolder, Packages.Num());
	return bSucceeded;
}

bool UCityToolLibrary::GenerateCity(
	const double CitySizeMeters,
	const double BlockSizeMeters,
	const double BuildingDensity,
	const double BuildingHeightMeters,
	const double BuildingVariation,
	const double ParkRatio,
	const double ParkTreeDensity,
	const double VegetationScaleVariation,
	const double ParkingOccupancy,
	const double ParkingClearanceMeters,
	const double CarVariation,
	const bool bStopSigns,
	const bool bYieldSigns,
	const bool bDirectionSigns,
	const bool bSpeedLimitSigns,
	const double SignDistanceMeters,
	const double SignSpacingMeters,
	const int32 Seed)
{
	using namespace CityTool;

	const FCityParams Params = MakeParams(CitySizeMeters, BlockSizeMeters, BuildingDensity, BuildingHeightMeters, BuildingVariation, ParkRatio, ParkTreeDensity, VegetationScaleVariation, ParkingOccupancy, ParkingClearanceMeters, CarVariation, bStopSigns, bYieldSigns, bDirectionSigns, bSpeedLimitSigns, SignDistanceMeters, SignSpacingMeters);
	FString Error;
	if (!Params.IsValid(&Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot generate the city: %s"), *Error);
		return false;
	}

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return false;
	}

	UPCGGraph* Graph = LoadCityAsset<UPCGGraph>(MainGraphName);
	if (!Graph && BuildCityGraphs())
	{
		Graph = LoadCityAsset<UPCGGraph>(MainGraphName);
	}
	if (!Graph)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load or build %s."), *GetObjectPath(MainGraphName));
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MarkelNunezTool", "GenerateCityTransaction", "Generate city"));
	APCGVolume* Volume = FindCityVolume(World);
	if (!Volume)
	{
		Volume = Cast<APCGVolume>(GEditor->AddActor(World->GetCurrentLevel(), APCGVolume::StaticClass(), FTransform::Identity));
		if (!Volume)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to spawn the City volume."));
			return false;
		}
		Volume->Tags.Add(CityTag);
		Volume->Tags.Add(CityGeneratedTag);
		Volume->SetActorLabel(TEXT("City"));
	}

	Volume->Modify();
	const double BoxScale = Params.CitySize / VolumeBrushSize;
	Volume->SetActorScale3D(FVector(BoxScale, BoxScale, FMath::Max(1.0, Params.BuildingHeight * 2.0 / VolumeBrushSize)));

	UPCGComponent* PCGComponent = Volume->PCGComponent;
	if (!PCGComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("The City volume has no PCG component."));
		return false;
	}

	PCGComponent->Modify();
	if (PCGComponent->GetGraph() != Graph)
	{
		PCGComponent->SetGraph(Graph);
	}

	UPCGGraphInterface* GraphInstance = PCGComponent->GetGraphInstance();
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("CitySize"), Params.CitySize);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("BlockSize"), Params.BlockSize);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("BuildingDensity"), Params.BuildingDensity);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("BuildingHeight"), Params.BuildingHeight);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("BuildingVariation"), Params.BuildingVariation);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("ParkRatio"), Params.ParkRatio);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("ParkTreeDensity"), Params.ParkTreeDensity);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("VegetationScaleVariation"), Params.VegetationScaleVariation);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("ParkingOccupancy"), Params.ParkingOccupancy);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("ParkingClearance"), Params.ParkingClearance);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("CarVariation"), Params.CarVariation);
	UPCGGraphParametersHelpers::SetBoolParameter(GraphInstance, TEXT("StopSigns"), Params.bStopSigns);
	UPCGGraphParametersHelpers::SetBoolParameter(GraphInstance, TEXT("YieldSigns"), Params.bYieldSigns);
	UPCGGraphParametersHelpers::SetBoolParameter(GraphInstance, TEXT("DirectionSigns"), Params.bDirectionSigns);
	UPCGGraphParametersHelpers::SetBoolParameter(GraphInstance, TEXT("SpeedLimitSigns"), Params.bSpeedLimitSigns);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("SignDistance"), Params.SignDistance);
	UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, TEXT("SignSpacing"), Params.SignSpacing);
	PCGComponent->Seed = Seed;
	PCGComponent->Generate(true);
	return true;
}

void UCityToolLibrary::CleanupCity()
{
	using namespace CityTool;

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MarkelNunezTool", "CleanupCityTransaction", "Clean up city"));
	if (APCGVolume* Volume = FindCityVolume(World))
	{
		if (Volume->PCGComponent)
		{
			Volume->PCGComponent->Cleanup(/*bRemoveComponents=*/true);
		}
	}

	TArray<AActor*> Generated;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(CityGeneratedTag))
		{
			Generated.Add(*It);
		}
	}
	for (AActor* Actor : Generated)
	{
		World->EditorDestroyActor(Actor, true);
	}
	UE_LOG(LogTemp, Display, TEXT("City cleaned up: removed %d actors."), Generated.Num());
}

bool UCityToolLibrary::TestDeterminism(
	FString& Report,
	const double CitySizeMeters,
	const double BlockSizeMeters,
	const double BuildingDensity,
	const double BuildingHeightMeters,
	const double BuildingVariation,
	const double ParkRatio,
	const double ParkTreeDensity,
	const double VegetationScaleVariation,
	const double ParkingOccupancy,
	const double ParkingClearanceMeters,
	const double CarVariation,
	const bool bStopSigns,
	const bool bYieldSigns,
	const bool bDirectionSigns,
	const bool bSpeedLimitSigns,
	const double SignDistanceMeters,
	const double SignSpacingMeters)
{
	const FCityParams Params = CityTool::MakeParams(CitySizeMeters, BlockSizeMeters, BuildingDensity, BuildingHeightMeters, BuildingVariation, ParkRatio, ParkTreeDensity, VegetationScaleVariation, ParkingOccupancy, ParkingClearanceMeters, CarVariation, bStopSigns, bYieldSigns, bDirectionSigns, bSpeedLimitSigns, SignDistanceMeters, SignSpacingMeters);
	FString Error;
	if (!Params.IsValid(&Error))
	{
		Report = Error;
		return false;
	}

	const FCityLayout First = CityLayout::Generate(Params, FVector::ZeroVector, 7);
	const FCityLayout Second = CityLayout::Generate(Params, FVector::ZeroVector, 7);
	const FCityLayout Other = CityLayout::Generate(Params, FVector::ZeroVector, 8);

	const bool bSameSeedMatches = CityLayout::AreEqual(First, Second);
	const bool bNewSeedDiffers = !CityLayout::AreEqual(First, Other);
	Report = FString::Printf(TEXT("Seed 7 twice: %s (%d buildings, %d lamps, %d trees, %d parks, %d cars, %d signs). Seed 8: %s (%d buildings)."),
		bSameSeedMatches ? TEXT("same city") : TEXT("DIFFERENT city"),
		First.Buildings.Num(), First.Lamps.Num(), First.Trees.Num(), First.Parks.Num(), First.Cars.Num(), First.Signs.Num(),
		bNewSeedDiffers ? TEXT("different city") : TEXT("SAME city"),
		Other.Buildings.Num());

	UE_LOG(LogTemp, Display, TEXT("City determinism: %s"), *Report);
	return bSameSeedMatches && bNewSeedDiffers;
}
