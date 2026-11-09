// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerlinPCGLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Elements/PCGCreatePointsGrid.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGTransformPoints.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Helpers/PCGGraphParametersHelpers.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGVolume.h"
#include "ScopedTransaction.h"

const TCHAR* const UPerlinPCGLibrary::DefaultCubeTreePath = TEXT("/PCGBiomeSample/Meshes/PCG_Pine_01.PCG_Pine_01");
const TCHAR* const UPerlinPCGLibrary::DefaultSphereTreePath = TEXT("/PCGBiomeSample/Meshes/PCG_Tree_01.PCG_Tree_01");

namespace
{
	const FName PerlinPCGTag(TEXT("PerlinPCG"));
	const TCHAR* const PerlinCubesGraphPath = TEXT("/MarkelNunezTool/PCG_PerlinCubes.PCG_PerlinCubes");
	const TCHAR* const PerlinTreesPackagePath = TEXT("/MarkelNunezTool/PCG_PerlinTrees");
	const TCHAR* const PerlinTreesAssetName = TEXT("PCG_PerlinTrees");
	const FSoftObjectPath CubeMeshPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	const FSoftObjectPath SphereMeshPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// The default PCG volume brush is 200 units wide.
	constexpr double VolumeBrushHalfSize = 100.0;

	UWorld* GetEditorWorld()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			UE_LOG(LogTemp, Error, TEXT("There is no active editor world to place the PCG volume in."));
		}
		return World;
	}

	UPCGGraph* LoadCubesGraph()
	{
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, PerlinCubesGraphPath);
		if (!Graph)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load the PCG graph %s."), PerlinCubesGraphPath);
		}
		return Graph;
	}

	UStaticMesh* LoadMeshOrDefault(UStaticMesh* Mesh, const TCHAR* DefaultPath)
	{
		if (Mesh)
		{
			return Mesh;
		}

		Mesh = LoadObject<UStaticMesh>(nullptr, DefaultPath);
		if (!Mesh)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load the default tree mesh %s."), DefaultPath);
		}
		return Mesh;
	}

	APCGVolume* FindPerlinVolume(UWorld* World)
	{
		for (TActorIterator<APCGVolume> It(World); It; ++It)
		{
			if (It->ActorHasTag(PerlinPCGTag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	// Sizes the volume to match the graph's point grid so its bounds show where the shapes will appear.
	FVector GetVolumeScale(const UPCGGraph* Graph)
	{
		for (const UPCGNode* Node : Graph->GetNodes())
		{
			if (const UPCGCreatePointsGridSettings* GridSettings = Cast<UPCGCreatePointsGridSettings>(Node->GetSettings()))
			{
				return (GridSettings->GridExtents / VolumeBrushHalfSize).ComponentMax(FVector::OneVector);
			}
		}
		return FVector::OneVector;
	}

	APCGVolume* GetOrSpawnPerlinVolume(UWorld* World, UPCGGraph* Graph)
	{
		if (APCGVolume* Existing = FindPerlinVolume(World))
		{
			return Existing;
		}

		const FTransform Transform(FQuat::Identity, FVector::ZeroVector, GetVolumeScale(Graph));
		APCGVolume* Volume = Cast<APCGVolume>(GEditor->AddActor(World->GetCurrentLevel(), APCGVolume::StaticClass(), Transform));
		if (!Volume)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to spawn the PCG volume."));
			return nullptr;
		}

		Volume->Tags.Add(PerlinPCGTag);
		Volume->SetActorLabel(TEXT("PerlinPCG"));
		return Volume;
	}

	void GenerateWithGraph(APCGVolume* Volume, UPCGGraph* Graph)
	{
		UPCGComponent* PCGComponent = Volume->PCGComponent;
		if (!PCGComponent)
		{
			UE_LOG(LogTemp, Error, TEXT("The PCG volume has no PCG component."));
			return;
		}

		PCGComponent->Modify();
		PCGComponent->SetGraph(Graph);
		PCGComponent->Generate(true);
	}

	UPCGMeshSelectorWeighted* GetWeightedSelector(const UPCGNode* Node)
	{
		const UPCGStaticMeshSpawnerSettings* Spawner = Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings());
		return Spawner ? Cast<UPCGMeshSelectorWeighted>(Spawner->MeshSelectorParameters) : nullptr;
	}

	UPCGGraph* GetOrCreateTreesGraph(UPCGGraph* SourceGraph)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PerlinTreesPackagePath, PerlinTreesAssetName);
		if (UPCGGraph* Existing = LoadObject<UPCGGraph>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}

		UPackage* Package = CreatePackage(PerlinTreesPackagePath);
		UPCGGraph* Graph = DuplicateObject<UPCGGraph>(SourceGraph, Package, PerlinTreesAssetName);
		if (!Graph)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to duplicate %s."), PerlinCubesGraphPath);
			return nullptr;
		}

		Graph->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Graph);
		Package->MarkPackageDirty();
		return Graph;
	}

	UPCGComponent* FindPerlinPCGComponent()
	{
		UWorld* World = GetEditorWorld();
		APCGVolume* Volume = World ? FindPerlinVolume(World) : nullptr;
		if (!Volume || !Volume->PCGComponent)
		{
			UE_LOG(LogTemp, Error, TEXT("There is no PerlinPCG volume in the level. Spawn it first."));
			return nullptr;
		}
		return Volume->PCGComponent;
	}

	// Returns the PCG component's graph instance if it exposes a parameter with that name and type.
	UPCGGraphInterface* FindParameterOwner(UPCGComponent* PCGComponent, const FName ParameterName, EPropertyBagPropertyType& OutType)
	{
		UPCGGraphInterface* GraphInstance = PCGComponent->GetGraphInstance();
		const FInstancedPropertyBag* Parameters = GraphInstance ? GraphInstance->GetUserParametersStruct() : nullptr;
		const FPropertyBagPropertyDesc* Desc = Parameters ? Parameters->FindPropertyDescByName(ParameterName) : nullptr;
		if (!Desc)
		{
			UE_LOG(LogTemp, Error, TEXT("The PCG graph has no parameter named %s."), *ParameterName.ToString());
			return nullptr;
		}

		OutType = Desc->ValueType;
		return GraphInstance;
	}

	void FinishParameterChange(UPCGComponent* PCGComponent, const bool bRegenerate)
	{
		if (bRegenerate)
		{
			PCGComponent->Generate(true);
		}
	}

	const UPCGNode* FindNodeByName(const UPCGGraph* Graph, const FName NodeName)
	{
		const UPCGNode* const* Node = Graph->GetNodes().FindByPredicate(
			[NodeName](const UPCGNode* Candidate) { return Candidate->GetFName() == NodeName; });
		return Node ? *Node : nullptr;
	}

	// Widens the X/Y offset range of every Transform Points node in the copy by Jitter, starting from the
	// original graph's values so repeated runs don't keep growing the range.
	int32 ApplyPositionJitter(const UPCGGraph* SourceGraph, const UPCGGraph* TreesGraph, const float Jitter)
	{
		int32 JitteredNodes = 0;
		for (const UPCGNode* SourceNode : SourceGraph->GetNodes())
		{
			const UPCGTransformPointsSettings* SourceSettings = Cast<UPCGTransformPointsSettings>(SourceNode->GetSettings());
			const UPCGNode* TreesNode = SourceSettings ? FindNodeByName(TreesGraph, SourceNode->GetFName()) : nullptr;
			UPCGTransformPointsSettings* TreesSettings = TreesNode ? Cast<UPCGTransformPointsSettings>(TreesNode->GetSettings()) : nullptr;
			if (!TreesSettings)
			{
				continue;
			}

			TreesSettings->Modify();
			TreesSettings->OffsetMin = SourceSettings->OffsetMin - FVector(Jitter, Jitter, 0.0);
			TreesSettings->OffsetMax = SourceSettings->OffsetMax + FVector(Jitter, Jitter, 0.0);
			TreesSettings->PostEditChange();
			++JitteredNodes;
		}
		return JitteredNodes;
	}
}

APCGVolume* UPerlinPCGLibrary::SpawnPerlinPCG()
{
	UWorld* World = GetEditorWorld();
	UPCGGraph* Graph = LoadCubesGraph();
	if (!World || !Graph)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT("MarkelNunezTool", "SpawnPerlinPCGTransaction", "Spawn Perlin PCG"));
	APCGVolume* Volume = GetOrSpawnPerlinVolume(World, Graph);
	if (Volume)
	{
		GenerateWithGraph(Volume, Graph);
	}
	return Volume;
}

bool UPerlinPCGLibrary::ReplaceShapesWithTrees(UStaticMesh* CubeTree, UStaticMesh* SphereTree, const float PositionJitter)
{
	UWorld* World = GetEditorWorld();
	UPCGGraph* SourceGraph = LoadCubesGraph();
	CubeTree = LoadMeshOrDefault(CubeTree, DefaultCubeTreePath);
	SphereTree = LoadMeshOrDefault(SphereTree, DefaultSphereTreePath);
	if (!World || !SourceGraph || !CubeTree || !SphereTree)
	{
		return false;
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT("MarkelNunezTool", "ReplaceWithTreesTransaction", "Replace Perlin shapes with trees"));
	UPCGGraph* TreesGraph = GetOrCreateTreesGraph(SourceGraph);
	if (!TreesGraph)
	{
		return false;
	}

	// The copy no longer holds cubes or spheres after the first swap, so read which entry is which
	// from the original graph; duplicated nodes keep their names.
	int32 ReplacedEntries = 0;
	for (const UPCGNode* SourceNode : SourceGraph->GetNodes())
	{
		const UPCGMeshSelectorWeighted* SourceSelector = GetWeightedSelector(SourceNode);
		if (!SourceSelector)
		{
			continue;
		}

		const UPCGNode* TreesNode = FindNodeByName(TreesGraph, SourceNode->GetFName());
		UPCGMeshSelectorWeighted* TreesSelector = TreesNode ? GetWeightedSelector(TreesNode) : nullptr;
		if (!TreesSelector || TreesSelector->MeshEntries.Num() != SourceSelector->MeshEntries.Num())
		{
			continue;
		}

		TreesNode->GetSettings()->Modify();
		TreesSelector->Modify();
		for (int32 Index = 0; Index < SourceSelector->MeshEntries.Num(); ++Index)
		{
			const FSoftObjectPath SourceMesh = SourceSelector->MeshEntries[Index].Descriptor.StaticMesh.ToSoftObjectPath();
			UStaticMesh* Tree = SourceMesh == CubeMeshPath ? CubeTree : SourceMesh == SphereMeshPath ? SphereTree : nullptr;
			if (Tree)
			{
				TreesSelector->MeshEntries[Index].Descriptor.StaticMesh = Tree;
				++ReplacedEntries;
			}
		}
		TreesNode->GetSettings()->PostEditChange();
	}

	if (ReplacedEntries == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No cube or sphere spawner entries were found in %s."), PerlinCubesGraphPath);
		return false;
	}

	if (ApplyPositionJitter(SourceGraph, TreesGraph, PositionJitter) == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No Transform Points nodes were found in %s, so tree positions were not randomized."), PerlinCubesGraphPath);
	}

	TreesGraph->MarkPackageDirty();
	if (APCGVolume* Volume = GetOrSpawnPerlinVolume(World, SourceGraph))
	{
		GenerateWithGraph(Volume, TreesGraph);
		return true;
	}
	return false;
}

bool UPerlinPCGLibrary::SetPerlinPCGNumber(const FName ParameterName, const float Value, const bool bRegenerate)
{
	UPCGComponent* PCGComponent = FindPerlinPCGComponent();
	EPropertyBagPropertyType Type = EPropertyBagPropertyType::None;
	UPCGGraphInterface* GraphInstance = PCGComponent ? FindParameterOwner(PCGComponent, ParameterName, Type) : nullptr;
	if (!GraphInstance)
	{
		return false;
	}

	switch (Type)
	{
	case EPropertyBagPropertyType::Float:
		UPCGGraphParametersHelpers::SetFloatParameter(GraphInstance, ParameterName, Value);
		break;
	case EPropertyBagPropertyType::Double:
		UPCGGraphParametersHelpers::SetDoubleParameter(GraphInstance, ParameterName, Value);
		break;
	case EPropertyBagPropertyType::Int32:
		UPCGGraphParametersHelpers::SetInt32Parameter(GraphInstance, ParameterName, FMath::RoundToInt32(Value));
		break;
	case EPropertyBagPropertyType::Int64:
		UPCGGraphParametersHelpers::SetInt64Parameter(GraphInstance, ParameterName, FMath::RoundToInt64(Value));
		break;
	default:
		UE_LOG(LogTemp, Error, TEXT("The PCG parameter %s is not a number."), *ParameterName.ToString());
		return false;
	}

	FinishParameterChange(PCGComponent, bRegenerate);
	return true;
}

bool UPerlinPCGLibrary::SetPerlinPCGVector(const FName ParameterName, const FVector Value, const bool bRegenerate)
{
	UPCGComponent* PCGComponent = FindPerlinPCGComponent();
	EPropertyBagPropertyType Type = EPropertyBagPropertyType::None;
	UPCGGraphInterface* GraphInstance = PCGComponent ? FindParameterOwner(PCGComponent, ParameterName, Type) : nullptr;
	if (!GraphInstance)
	{
		return false;
	}

	if (Type != EPropertyBagPropertyType::Struct)
	{
		UE_LOG(LogTemp, Error, TEXT("The PCG parameter %s is not a vector."), *ParameterName.ToString());
		return false;
	}

	UPCGGraphParametersHelpers::SetVectorParameter(GraphInstance, ParameterName, Value);
	FinishParameterChange(PCGComponent, bRegenerate);
	return true;
}

void UPerlinPCGLibrary::RegeneratePerlinPCG()
{
	if (UPCGComponent* PCGComponent = FindPerlinPCGComponent())
	{
		PCGComponent->Generate(true);
	}
}
