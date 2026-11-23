// Copyright Epic Games, Inc. All Rights Reserved.

#include "MarkelNunezToolEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "PerlinPCGLibrary.h"

IMPLEMENT_MODULE(FMarkelNunezToolEditorModule, MarkelNunezToolEditor)

void FMarkelNunezToolEditorModule::StartupModule()
{
	CubeTreeMesh = FSoftObjectPath(UPerlinPCGLibrary::DefaultCubeTreePath);
	SphereTreeMesh = FSoftObjectPath(UPerlinPCGLibrary::DefaultSphereTreePath);

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(PerlinTabName, FOnSpawnTab::CreateRaw(this, &FMarkelNunezToolEditorModule::OnSpawnPerlinTab))
		.SetDisplayName(NSLOCTEXT("MarkelNunezTool", "PerlinTabTitle", "Perlin Tools"))
		.SetMenuType(ETabSpawnerMenuType::Enabled);

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(CityTabName, FOnSpawnTab::CreateRaw(this, &FMarkelNunezToolEditorModule::OnSpawnCityTab))
		.SetDisplayName(NSLOCTEXT("MarkelNunezTool", "CityTabTitle", "City Generator"))
		.SetMenuType(ETabSpawnerMenuType::Enabled);
}

void FMarkelNunezToolEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PerlinTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CityTabName);
	PerlinTexture.Reset();
}
