// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class SDockTab;
class FSpawnTabArgs;
class UTexture2D;

class FMarkelNunezToolEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> OnSpawnPerlinTab(const FSpawnTabArgs& Args);
	void RegeneratePerlin();
	int32 GenerateCubesActors();
	int32 GenerateCubesISM();
	void SpawnOscillator();
	class UInstancedStaticMeshComponent* GetOrCreateISM(FName ComponentName, class UStaticMesh* Mesh);

	int32 Width = 256;
	int32 Height = 256;
	float Scale = 0.05f;
	int32 Seed = 0;
	float Threshold = 0.6f;
	float SphereThreshold = 0.75f;
	float Spacing = 120.0f;
	int32 NumGeneratedCubes = 0;
	FSoftObjectPath CubeTreeMesh;
	FSoftObjectPath SphereTreeMesh;

	TStrongObjectPtr<UTexture2D> PerlinTexture;
	FSlateBrush PerlinBrush;

	static const FName PerlinTabName;
};
