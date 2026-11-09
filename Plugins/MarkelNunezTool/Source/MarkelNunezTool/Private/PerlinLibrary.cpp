// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerlinLibrary.h"

#include "Engine/Texture2D.h"

float UPerlinLibrary::GetPerlinValue(const int32 X, const int32 Y, const float Scale, const int32 Seed)
{
	const float SafeScale = FMath::Max(Scale, UE_SMALL_NUMBER);
	const FVector2D SamplePoint(X * SafeScale + Seed * 31.7f,Y * SafeScale + Seed * 17.3f);

	return FMath::Clamp((FMath::PerlinNoise2D(SamplePoint) + 1.0f) * 0.5f, 0.0f, 1.0f);
}

UTexture2D* UPerlinLibrary::GeneratePerlinTexture(
	const int32 Width,
	const int32 Height,
	const float Scale,
	const int32 Seed)
{
	const int32 SafeWidth = FMath::Clamp(Width, 8, 4096);
	const int32 SafeHeight = FMath::Clamp(Height, 8, 4096);
	UTexture2D* Texture = UTexture2D::CreateTransient(SafeWidth, SafeHeight, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.IsEmpty())
	{
		return nullptr;
	}

	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Pixels)
	{
		return nullptr;
	}

	for (int32 Y = 0; Y < SafeHeight; ++Y)
	{
		for (int32 X = 0; X < SafeWidth; ++X)
		{
			const uint8 Value = static_cast<uint8>(
				FMath::RoundToInt(GetPerlinValue(X, Y, Scale, Seed) * 255.0f));
			Pixels[Y * SafeWidth + X] = FColor(Value, Value, Value, 255);
		}
	}

	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}
