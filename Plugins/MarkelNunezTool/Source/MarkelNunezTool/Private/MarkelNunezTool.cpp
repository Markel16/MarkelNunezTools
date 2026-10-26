// Copyright Epic Games, Inc. All Rights Reserved.

#include "MarkelNunezTool.h"

#define LOCTEXT_NAMESPACE "FMarkelNunezToolModule"

void FMarkelNunezToolModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FMarkelNunezToolModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMarkelNunezToolModule, MarkelNunezTool)

UTexture2D* UPerlinLibrary::GeneratePerlinTexture(int32 Width, int32 Height, float Scale, int32 Seed)
{
Width = FMath::Clamp(Width, 8, 4096); Height = FMath::Clamp(Height, 8, 4096);
UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
if (!Tex) { return nullptr; }
FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
for (int32 Y = 0; Y < Height; ++Y)
for (int32 X = 0; X < Width; ++X)
{
const uint8 V = (uint8)FMath::RoundToInt(GetPerlinValue(X, Y, Scale, Seed) * 255.f);
Pixels[Y * Width + X] = FColor(V, V, V, 255);
}
Mip.BulkData.Unlock();
Tex->UpdateResource(); // uploads the pixels to the GPU
return Tex;
}