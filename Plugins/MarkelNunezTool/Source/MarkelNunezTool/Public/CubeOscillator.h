// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CubeOscillator.generated.h"

class UInstancedStaticMeshComponent;

UCLASS()
class MARKELNUNEZTOOL_API ACubeOscillator : public AActor
{
	GENERATED_BODY()

public:
	ACubeOscillator();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Oscillator")
	float Amplitude = 50.0f;

	// Each cube gets a random speed in [MinSpeed, MaxSpeed] and a random phase, so they move out of sync.
	UPROPERTY(EditAnywhere, Category = "Oscillator", meta = (ClampMin = "0.0"))
	float MinSpeed = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Oscillator", meta = (ClampMin = "0.0"))
	float MaxSpeed = 4.0f;

private:
	struct FOscillation
	{
		float Speed = 1.0f;
		float Phase = 0.0f;
	};

	struct FInstanceGroup
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> ISM;
		TArray<FTransform> BaseTransforms;
		TArray<FOscillation> Oscillations;
	};

	void CollectActors();
	void CollectInstances();
	FOscillation MakeOscillation(const FVector& Position) const;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Cubes;

	TArray<FVector> BasePositions;
	TArray<FOscillation> ActorOscillations;

	TArray<FInstanceGroup> InstanceGroups;
	float Time = 0.0f;
};
