// Copyright Epic Games, Inc. All Rights Reserved.

#include "CubeOscillator.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	const FName PerlinCubeTag(TEXT("PerlinCube"));
	const FName PerlinCubesISMTag(TEXT("PerlinCubesISM"));
}

ACubeOscillator::ACubeOscillator()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ACubeOscillator::BeginPlay()
{
	Super::BeginPlay();
	CollectActors();
	CollectInstances();
}

ACubeOscillator::FOscillation ACubeOscillator::MakeOscillation(const FVector& Position) const
{
	// Seeded from the position so every cube keeps the same speed and phase between plays.
	const FRandomStream Random(static_cast<int32>(HashCombine(GetTypeHash(FMath::RoundToInt(Position.X)),GetTypeHash(FMath::RoundToInt(Position.Y)))));

	FOscillation Oscillation;
	Oscillation.Speed = Random.FRandRange(FMath::Min(MinSpeed, MaxSpeed), FMath::Max(MinSpeed, MaxSpeed));
	Oscillation.Phase = Random.FRandRange(0.0f, UE_TWO_PI);
	return Oscillation;
}

void ACubeOscillator::CollectActors()
{
	Cubes.Reset();
	BasePositions.Reset();
	ActorOscillations.Reset();

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor->ActorHasTag(PerlinCubeTag))
		{
			continue;
		}

		if (USceneComponent* ActorRoot = Actor->GetRootComponent())
		{
			ActorRoot->SetMobility(EComponentMobility::Movable);
		}

		Cubes.Add(Actor);
		BasePositions.Add(Actor->GetActorLocation());
		ActorOscillations.Add(MakeOscillation(Actor->GetActorLocation()));
	}
}

void ACubeOscillator::CollectInstances()
{
	InstanceGroups.Reset();

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(PerlinCubesISMTag))
		{
			continue;
		}

		TArray<UInstancedStaticMeshComponent*> Components;
		It->GetComponents(Components);
		for (UInstancedStaticMeshComponent* ISM : Components)
		{
			ISM->SetMobility(EComponentMobility::Movable);

			FInstanceGroup& Group = InstanceGroups.AddDefaulted_GetRef();
			Group.ISM = ISM;
			for (int32 Index = 0; Index < ISM->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				if (ISM->GetInstanceTransform(Index, Transform))
				{
					Group.BaseTransforms.Add(Transform);
					Group.Oscillations.Add(MakeOscillation(Transform.GetLocation()));
				}
			}
		}
	}
}

void ACubeOscillator::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TRACE_CPUPROFILER_EVENT_SCOPE(CubeOscillator_Tick);

	Time += DeltaSeconds;
	const auto WaveHeight = [this](const FOscillation& Oscillation) -> float
	{
		return FMath::Sin(Time * Oscillation.Speed + Oscillation.Phase) * Amplitude;
	};

	for (int32 Index = 0; Index < Cubes.Num(); ++Index)
	{
		if (IsValid(Cubes[Index]))
		{
			Cubes[Index]->SetActorLocation(BasePositions[Index] + FVector(0.0f, 0.0f, WaveHeight(ActorOscillations[Index])));
		}
	}

	for (const FInstanceGroup& Group : InstanceGroups)
	{
		UInstancedStaticMeshComponent* ISM = Group.ISM.Get();
		if (!IsValid(ISM) || ISM->GetInstanceCount() != Group.BaseTransforms.Num())
		{
			continue;
		}

		TArray<FTransform> UpdatedTransforms = Group.BaseTransforms;
		for (int32 Index = 0; Index < UpdatedTransforms.Num(); ++Index)
		{
			UpdatedTransforms[Index].AddToTranslation(FVector(0.0f, 0.0f, WaveHeight(Group.Oscillations[Index])));
		}

		ISM->BatchUpdateInstancesTransforms(0, UpdatedTransforms, false, true);
	}
}
