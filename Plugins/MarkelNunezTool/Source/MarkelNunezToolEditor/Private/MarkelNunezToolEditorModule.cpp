// Copyright Epic Games, Inc. All Rights Reserved.

#include "MarkelNunezToolEditorModule.h"

#include "Framework/Docking/TabManager.h"

IMPLEMENT_MODULE(FMarkelNunezToolEditorModule, MarkelNunezToolEditor)

void FMarkelNunezToolEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(PerlinTabName, FOnSpawnTab::CreateRaw(this, &FMarkelNunezToolEditorModule::OnSpawnPerlinTab))
		.SetDisplayName(NSLOCTEXT("MarkelNunezTool", "PerlinTabTitle", "Perlin Tools"))
		.SetMenuType(ETabSpawnerMenuType::Enabled);
}

void FMarkelNunezToolEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PerlinTabName);
	PerlinTexture.Reset();
}
