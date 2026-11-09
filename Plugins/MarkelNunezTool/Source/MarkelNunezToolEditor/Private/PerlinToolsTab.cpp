// Copyright Epic Games, Inc. All Rights Reserved.

#include "MarkelNunezToolEditorModule.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "CubeOscillator.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Framework/Docking/TabManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ScopedTransaction.h"
#include "PerlinLibrary.h"
#include "PerlinPCGLibrary.h"
#include "PropertyCustomizationHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Docking/SDockTab.h"

const FName FMarkelNunezToolEditorModule::PerlinTabName(TEXT("MarkelNunezPerlinTab"));

namespace
{
	const FName PerlinCubesISMTag(TEXT("PerlinCubesISM"));
	const FName PerlinCubeTag(TEXT("PerlinCube"));
	constexpr int64 MaxGridSamples = 1000000;
	constexpr int64 MaxActorSamples = 10000;

	UWorld* GetEditorWorld()
	{
		if (!GEditor)
		{
			UE_LOG(LogTemp, Error, TEXT("Cannot generate cubes because the editor is unavailable."));
			return nullptr;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			UE_LOG(LogTemp, Error, TEXT("Cannot generate cubes because there is no active editor world."));
		}
		return World;
	}

	UStaticMesh* LoadEngineMesh(const TCHAR* Path)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!Mesh)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load the engine mesh %s."), Path);
		}
		return Mesh;
	}

	UStaticMesh* LoadCubeMesh()
	{
		return LoadEngineMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	UStaticMesh* LoadSphereMesh()
	{
		return LoadEngineMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}

	struct FShapeTransforms
	{
		TArray<FTransform> Cubes;
		TArray<FTransform> Spheres;
	};

	// Samples above Threshold spawn a shape; samples above SphereThreshold become spheres instead of cubes.
	FShapeTransforms BuildShapeTransforms(
		const int32 Width,
		const int32 Height,
		const float Scale,
		const int32 Seed,
		const float Threshold,
		const float SphereThreshold,
		const float Spacing)
	{
		FShapeTransforms Transforms;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float Value = UPerlinLibrary::GetPerlinValue(X, Y, Scale, Seed);
				if (Value <= Threshold)
				{
					continue;
				}

				const FTransform Transform(FVector(X * Spacing, Y * Spacing, 0.0f));
				if (Value > SphereThreshold)
				{
					Transforms.Spheres.Add(Transform);
				}
				else
				{
					Transforms.Cubes.Add(Transform);
				}
			}
		}
		return Transforms;
	}

	template <typename T>
	TSharedRef<SWidget> MakeParameterRow(const FText& Label, T& Value, const T MinValue, const T MaxValue)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<T>)
				.MinValue(MinValue)
				.MaxValue(MaxValue)
				.Value_Lambda([&Value]() { return Value; })
				.OnValueChanged_Lambda([&Value](const T NewValue) { Value = NewValue; })
			];
	}

	TSharedRef<SWidget> MakeMeshRow(const FText& Label, FSoftObjectPath& MeshPath)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UStaticMesh::StaticClass())
				.ObjectPath_Lambda([&MeshPath]() { return MeshPath.ToString(); })
				.OnObjectChanged_Lambda([&MeshPath](const FAssetData& AssetData) { MeshPath = AssetData.GetSoftObjectPath(); })
			];
	}
}

void FMarkelNunezToolEditorModule::RegeneratePerlin()
{
	UTexture2D* GeneratedTexture = UPerlinLibrary::GeneratePerlinTexture(Width, Height, Scale, Seed);
	if (!GeneratedTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to generate the Perlin texture."));
		return;
	}

	PerlinTexture.Reset(GeneratedTexture);
	PerlinBrush.SetResourceObject(PerlinTexture.Get());
	PerlinBrush.ImageSize = FVector2D(Width, Height);
}

TSharedRef<SDockTab> FMarkelNunezToolEditorModule::OnSpawnPerlinTab(const FSpawnTabArgs& Args)
{
	RegeneratePerlin();

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeParameterRow(NSLOCTEXT("MarkelNunezTool", "WidthLabel", "Width"), Width, 8, 2048)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeParameterRow(NSLOCTEXT("MarkelNunezTool", "HeightLabel", "Height"), Height, 8, 2048)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeParameterRow(NSLOCTEXT("MarkelNunezTool", "ScaleLabel", "Scale"), Scale, 0.001f, 0.5f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeParameterRow(NSLOCTEXT("MarkelNunezTool", "SeedLabel", "Seed"), Seed, 0, 9999)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeParameterRow(NSLOCTEXT("MarkelNunezTool", "ThresholdLabel", "Threshold"), Threshold, 0.0f, 1.0f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeParameterRow(NSLOCTEXT("MarkelNunezTool", "SphereThresholdLabel", "Sphere Threshold"), SphereThreshold, 0.0f, 1.0f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeParameterRow(NSLOCTEXT("MarkelNunezTool", "SpacingLabel", "Spacing"), Spacing, 50.0f, 300.0f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MarkelNunezTool", "GenerateTextureButton", "Generate texture"))
				.OnClicked_Lambda([this]()
				{
					RegeneratePerlin();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MarkelNunezTool", "GenerateCubesButton", "Generate cubes (ISM)"))
				.OnClicked_Lambda([this]()
				{
					NumGeneratedCubes = GenerateCubesISM();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MarkelNunezTool", "GenerateActorCubesButton", "Generate cubes (actors)"))
				.OnClicked_Lambda([this]()
				{
					NumGeneratedCubes = GenerateCubesActors();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MarkelNunezTool", "SpawnOscillatorButton", "Add cube oscillator"))
				.OnClicked_Lambda([this]()
				{
					SpawnOscillator();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 12.0f, 4.0f, 4.0f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("MarkelNunezTool", "PCGHeader", "PCG"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MarkelNunezTool", "SpawnPCGButton", "Spawn PCG Perlin"))
				.OnClicked_Lambda([]()
				{
					UPerlinPCGLibrary::SpawnPerlinPCG();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeMeshRow(NSLOCTEXT("MarkelNunezTool", "CubeTreeLabel", "Cube tree"), CubeTreeMesh)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeMeshRow(NSLOCTEXT("MarkelNunezTool", "SphereTreeLabel", "Sphere tree"), SphereTreeMesh)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("MarkelNunezTool", "ReplaceWithTreesButton", "Replace with trees"))
				.OnClicked_Lambda([this]()
				{
					UPerlinPCGLibrary::ReplaceShapesWithTrees(
						Cast<UStaticMesh>(CubeTreeMesh.TryLoad()),
						Cast<UStaticMesh>(SphereTreeMesh.TryLoad()));
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::Format(
						NSLOCTEXT("MarkelNunezTool", "GeneratedCubeCount", "Generated cubes: {0}"),
						FText::AsNumber(NumGeneratedCubes));
				})
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SImage).Image(&PerlinBrush)
				]
			]
		];
}

UInstancedStaticMeshComponent* FMarkelNunezToolEditorModule::GetOrCreateISM(const FName ComponentName, UStaticMesh* Mesh)
{
	UWorld* World = GetEditorWorld();
	if (!World || !Mesh)
	{
		return nullptr;
	}

	AActor* Container = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(PerlinCubesISMTag))
		{
			Container = *It;
			break;
		}
	}

	if (!Container)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transactional;
		Container = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!Container)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to create the ISM container actor."));
			return nullptr;
		}

		Container->Tags.Add(PerlinCubesISMTag);
		Container->SetActorLabel(TEXT("PerlinCubes"));
	}

	Container->Modify();
	UInstancedStaticMeshComponent* ISM = nullptr;
	TArray<UInstancedStaticMeshComponent*> ExistingISMs;
	Container->GetComponents(ExistingISMs);
	for (UInstancedStaticMeshComponent* ExistingISM : ExistingISMs)
	{
		if (ExistingISM->GetFName() == ComponentName)
		{
			ISM = ExistingISM;
			break;
		}
	}

	if (!ISM)
	{
		ISM = NewObject<UInstancedStaticMeshComponent>(Container, ComponentName, RF_Transactional);
		if (!ISM)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to create the instanced mesh component."));
			return nullptr;
		}

		if (USceneComponent* ContainerRoot = Container->GetRootComponent())
		{
			ISM->SetupAttachment(ContainerRoot);
		}
		else
		{
			Container->SetRootComponent(ISM);
		}
		Container->AddInstanceComponent(ISM);
		ISM->RegisterComponent();
	}

	ISM->SetStaticMesh(Mesh);
	ISM->SetMobility(EComponentMobility::Movable);
	return ISM;
}

int32 FMarkelNunezToolEditorModule::GenerateCubesActors()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MarkelNunezTool_GenerateCubesActors);
	TRACE_BOOKMARK(TEXT("Generate actor cubes %d x %d"), Width, Height);

	const int64 GridSamples = static_cast<int64>(Width) * Height;
	if (Width < 1 || Height < 1 || Spacing <= 0.0f || GridSamples > MaxActorSamples)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Actor generation requires positive dimensions and spacing, and at most %lld grid samples."),
			MaxActorSamples);
		return NumGeneratedCubes;
	}

	UWorld* World = GetEditorWorld();
	UStaticMesh* CubeMesh = LoadCubeMesh();
	UStaticMesh* SphereMesh = LoadSphereMesh();
	if (!World || !CubeMesh || !SphereMesh)
	{
		return NumGeneratedCubes;
	}

	const FShapeTransforms Transforms =
		BuildShapeTransforms(Width, Height, Scale, Seed, Threshold, SphereThreshold, Spacing);
	const FScopedTransaction Transaction(
		NSLOCTEXT("MarkelNunezTool", "GenerateActorCubesTransaction", "Generate Perlin actor cubes"));

	TArray<AActor*> PreviousCubes;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(PerlinCubeTag))
		{
			PreviousCubes.Add(*It);
		}
	}

	for (AActor* PreviousCube : PreviousCubes)
	{
		World->EditorDestroyActor(PreviousCube, true);
	}

	int32 SpawnedCubeCount = 0;
	const auto SpawnShapes = [&](const TArray<FTransform>& ShapeTransforms, UStaticMesh* Mesh)
	{
		for (const FTransform& Transform : ShapeTransforms)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.ObjectFlags |= RF_Transactional;
			AStaticMeshActor* ShapeActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, SpawnParameters);
			if (!ShapeActor)
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to spawn a generated shape actor."));
				continue;
			}

			ShapeActor->Tags.Add(PerlinCubeTag);
			ShapeActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			ShapeActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			++SpawnedCubeCount;
		}
	};

	SpawnShapes(Transforms.Cubes, CubeMesh);
	SpawnShapes(Transforms.Spheres, SphereMesh);
	return SpawnedCubeCount;
}

int32 FMarkelNunezToolEditorModule::GenerateCubesISM()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MarkelNunezTool_GenerateCubesISM);
	TRACE_BOOKMARK(TEXT("Generate ISM cubes %d x %d"), Width, Height);

	const int64 GridSamples = static_cast<int64>(Width) * Height;
	if (Width < 1 || Height < 1 || Spacing <= 0.0f || GridSamples > MaxGridSamples)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("ISM generation requires positive dimensions and spacing, and at most %lld grid samples."),
			MaxGridSamples);
		return NumGeneratedCubes;
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT("MarkelNunezTool", "GenerateISMTransaction", "Generate Perlin ISM cubes"));
	UInstancedStaticMeshComponent* CubesISM = GetOrCreateISM(TEXT("PerlinCubesISM"), LoadCubeMesh());
	UInstancedStaticMeshComponent* SpheresISM = GetOrCreateISM(TEXT("PerlinSpheresISM"), LoadSphereMesh());
	if (!CubesISM || !SpheresISM)
	{
		return NumGeneratedCubes;
	}

	CubesISM->Modify();
	CubesISM->ClearInstances();
	SpheresISM->Modify();
	SpheresISM->ClearInstances();

	const FShapeTransforms Transforms =
		BuildShapeTransforms(Width, Height, Scale, Seed, Threshold, SphereThreshold, Spacing);

	CubesISM->AddInstances(Transforms.Cubes, false, false);
	SpheresISM->AddInstances(Transforms.Spheres, false, false);
	return CubesISM->GetInstanceCount() + SpheresISM->GetInstanceCount();
}

void FMarkelNunezToolEditorModule::SpawnOscillator()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ACubeOscillator> It(World); It; ++It)
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(*It, true, true);
		UE_LOG(LogTemp, Display, TEXT("A cube oscillator already exists in the level; selected it."));
		return;
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT("MarkelNunezTool", "SpawnOscillatorTransaction", "Add cube oscillator"));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transactional;
	ACubeOscillator* Oscillator = World->SpawnActor<ACubeOscillator>(
		ACubeOscillator::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!Oscillator)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn the cube oscillator."));
		return;
	}

	Oscillator->SetActorLabel(TEXT("CubeOscillator"));
	GEditor->SelectNone(false, true);
	GEditor->SelectActor(Oscillator, true, true);
}
