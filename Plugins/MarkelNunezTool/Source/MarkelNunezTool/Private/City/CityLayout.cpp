// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CityLayout.h"

#include "Math/RandomStream.h"

namespace
{
	constexpr int32 MaxCrossingsPerSide = 100;

	// Shapes that only touch along an edge (a street ending where a block starts) do not count as overlapping.
	constexpr double OverlapTolerance = 1.0;

	// Separate salts so the density draw and the building shape draw of the same lot are independent.
	constexpr uint32 DensitySalt = 0x1A2B3C4D;
	constexpr uint32 BuildingSalt = 0x5E6F7081;
	constexpr uint32 PropSalt = 0x92A3B4C5;
	constexpr uint32 ParkSalt = 0xD6E7F809;
	constexpr uint32 VegetationSalt = 0x1B2C3D4E;
	constexpr uint32 CarSalt = 0x5F607182;

	// Car models built from basic shapes: body and cabin sizes (length along the car, width, height) and
	// how far the cabin sits behind the body center. The first one is used when CarVariation is 0.
	struct FCarModel
	{
		FVector Body;
		FVector Cabin;
		double CabinOffset;
	};
	const FCarModel CarModels[] = {
		{ FVector(460.0, 180.0, 65.0), FVector(230.0, 165.0, 55.0), -30.0 },  // Sedan
		{ FVector(380.0, 170.0, 65.0), FVector(200.0, 155.0, 55.0), -20.0 },  // Compact
		{ FVector(470.0, 190.0, 90.0), FVector(290.0, 180.0, 65.0), -40.0 },  // SUV
		{ FVector(520.0, 200.0, 115.0), FVector(400.0, 195.0, 85.0), -40.0 }, // Van
	};

	const FLinearColor CarPalette[] = {
		FLinearColor(0.55f, 0.56f, 0.58f), // Silver
		FLinearColor(0.85f, 0.85f, 0.85f), // White
		FLinearColor(0.02f, 0.02f, 0.02f), // Black
		FLinearColor(0.50f, 0.03f, 0.03f), // Red
		FLinearColor(0.04f, 0.10f, 0.45f), // Blue
		FLinearColor(0.80f, 0.60f, 0.05f), // Yellow
		FLinearColor(0.05f, 0.30f, 0.10f), // Green
		FLinearColor(0.70f, 0.25f, 0.05f), // Orange
	};

	const FName TireLook(TEXT("Tire"));
	const FName SignPoleLook(TEXT("SignPole"));
	const FName SignRedLook(TEXT("SignRed"));
	const FName SignWhiteLook(TEXT("SignWhite"));
	const FName SignBlueLook(TEXT("SignBlue"));

	FName GetCarLook(const int32 ColorIndex)
	{
		return FName(*FString::Printf(TEXT("Car%d"), ColorIndex));
	}

	// A street segment seen from its own axis: Side points to the left of Axis.
	struct FStreetFrame
	{
		FVector2D Center;
		FVector2D Axis;
		FVector2D Side;
		double Length = 0.0;
		double Width = 0.0;
		double TopZ = 0.0;
		double AxisYaw = 0.0;

		explicit FStreetFrame(const FCityPiece& Street)
		{
			const FBox Box = Street.GetWorldBox();
			const FVector Size = Box.GetSize();
			const bool bAlongX = Size.X >= Size.Y;
			Center = FVector2D(Box.GetCenter());
			Axis = bAlongX ? FVector2D(1.0, 0.0) : FVector2D(0.0, 1.0);
			Side = FVector2D(-Axis.Y, Axis.X);
			Length = bAlongX ? Size.X : Size.Y;
			Width = bAlongX ? Size.Y : Size.X;
			TopZ = Box.Max.Z;
			AxisYaw = bAlongX ? 0.0 : 90.0;
		}

		// An axis-aligned box in the street, Along the axis and SideOffset towards the left.
		FCityPiece MakeBox(const double Along, const double SideOffset, const double AlongSize, const double SideSize, const double CenterZ, const double Height) const
		{
			const FVector2D BoxCenter = Center + Axis * Along + Side * SideOffset;
			const FVector2D Size2D = Axis.X != 0.0 ? FVector2D(AlongSize, SideSize) : FVector2D(SideSize, AlongSize);
			return FCityPiece::MakeBox(FVector(BoxCenter, CenterZ), FVector(Size2D, Height));
		}
	};

	FRandomStream MakeStream(const int32 PieceSeed, const int32 GraphSeed, const uint32 Salt)
	{
		return FRandomStream(static_cast<int32>(HashCombine(HashCombine(static_cast<uint32>(PieceSeed), static_cast<uint32>(GraphSeed)), Salt)));
	}

	// A plant with its pivot at the base of the trunk and a box of the given mesh-unit width and height.
	FCityPiece MakePlant(const FVector& Base, const double Scale, const double Yaw, const double WidthLocal, const double HeightLocal)
	{
		FCityPiece Plant;
		Plant.Location = Base;
		Plant.Yaw = Yaw;
		Plant.Scale = FVector(Scale);
		Plant.LocalBounds = FBox(FVector(-WidthLocal / 2.0, -WidthLocal / 2.0, 0.0), FVector(WidthLocal / 2.0, WidthLocal / 2.0, HeightLocal));
		return Plant;
	}

	FVector GetSize(const FCityPiece& Piece)
	{
		return Piece.GetWorldBox().GetSize();
	}

	bool Overlaps2D(const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X < B.Max.X - OverlapTolerance && B.Min.X < A.Max.X - OverlapTolerance
			&& A.Min.Y < B.Max.Y - OverlapTolerance && B.Min.Y < A.Max.Y - OverlapTolerance;
	}

	// Buckets footprints in a coarse grid so overlap tests only compare nearby shapes.
	class FFootprintGrid
	{
	public:
		explicit FFootprintGrid(const double InCellSize) : CellSize(InCellSize) {}

		void Add(const FBox2D& Footprint)
		{
			const int32 Index = Footprints.Add(Footprint);
			ForEachCell(Footprint, [this, Index](const FIntPoint& Cell) { Cells.FindOrAdd(Cell).Add(Index); });
		}

		bool OverlapsAny(const FBox2D& Footprint) const
		{
			bool bOverlaps = false;
			ForEachCell(Footprint, [this, &Footprint, &bOverlaps](const FIntPoint& Cell)
			{
				if (const TArray<int32>* Indices = Cells.Find(Cell))
				{
					for (const int32 Index : *Indices)
					{
						bOverlaps = bOverlaps || Overlaps2D(Footprints[Index], Footprint);
					}
				}
			});
			return bOverlaps;
		}

	private:
		template <typename FunctionType>
		void ForEachCell(const FBox2D& Footprint, FunctionType&& Function) const
		{
			const FIntPoint Min(FMath::FloorToInt(Footprint.Min.X / CellSize), FMath::FloorToInt(Footprint.Min.Y / CellSize));
			const FIntPoint Max(FMath::FloorToInt(Footprint.Max.X / CellSize), FMath::FloorToInt(Footprint.Max.Y / CellSize));
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					Function(FIntPoint(X, Y));
				}
			}
		}

		double CellSize;
		TArray<FBox2D> Footprints;
		TMap<FIntPoint, TArray<int32>> Cells;
	};

	TMap<FIntPoint, const FCityPiece*> MapByCell(TConstArrayView<FCityPiece> Crossings, int32& OutCount)
	{
		TMap<FIntPoint, const FCityPiece*> ByCell;
		OutCount = 0;
		for (const FCityPiece& Crossing : Crossings)
		{
			ByCell.Add(FIntPoint(Crossing.Col, Crossing.Row), &Crossing);
			OutCount = FMath::Max(OutCount, FMath::Max(Crossing.Col, Crossing.Row) + 1);
		}
		return ByCell;
	}

	bool ArePiecesEqual(TConstArrayView<FCityPiece> A, TConstArrayView<FCityPiece> B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!A[Index].Location.Equals(B[Index].Location) || !A[Index].Scale.Equals(B[Index].Scale)
				|| !FMath::IsNearlyEqual(A[Index].Yaw, B[Index].Yaw))
			{
				return false;
			}
		}
		return true;
	}
}

int32 FCityParams::CrossingsPerSide() const
{
	return Cell() > 0.0 ? FMath::FloorToInt(CitySize / Cell()) + 1 : 0;
}

bool FCityParams::IsValid(FString* OutError) const
{
	FString Error;
	if (CitySize <= 0.0 || BlockSize <= 0.0 || StreetWidth <= 0.0 || LotSize <= 0.0 || PropSpacing <= 0.0)
	{
		Error = TEXT("CitySize, BlockSize, StreetWidth, LotSize and PropSpacing must be positive.");
	}
	else if (BlockSize <= 2.0 * SidewalkWidth)
	{
		Error = TEXT("BlockSize must be larger than two sidewalks.");
	}
	else if (CrossingsPerSide() > MaxCrossingsPerSide)
	{
		Error = FString::Printf(TEXT("The city would have more than %d crossings per side. Increase BlockSize or reduce CitySize."), MaxCrossingsPerSide);
	}

	if (OutError)
	{
		*OutError = Error;
	}
	return Error.IsEmpty();
}

FCityPiece FCityPiece::MakeBox(const FVector& Center, const FVector& Size, const double Yaw)
{
	FCityPiece Piece;
	Piece.Location = Center;
	Piece.Yaw = Yaw;
	Piece.Scale = Size.ComponentMax(FVector(0.01)) / 100.0;
	Piece.Seed = CityLayout::SeedFromPosition(Center);
	return Piece;
}

FTransform FCityPiece::GetTransform() const
{
	return FTransform(FRotator(Pitch, Yaw, Roll), Location, Scale);
}

FBox FCityPiece::GetWorldBox() const
{
	return LocalBounds.TransformBy(GetTransform());
}

FBox2D FCityPiece::GetFootprint() const
{
	const FBox WorldBox = GetWorldBox();
	return FBox2D(FVector2D(WorldBox.Min), FVector2D(WorldBox.Max));
}

int32 CityLayout::SeedFromPosition(const FVector& Position)
{
	uint32 Hash = GetTypeHash(FMath::RoundToInt64(Position.X));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt64(Position.Y)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt64(Position.Z)));
	return static_cast<int32>(Hash);
}

TArray<FCityPiece> CityLayout::BuildCrossings(const FCityParams& Params, const FVector& Origin)
{
	TArray<FCityPiece> Crossings;
	if (!Params.IsValid())
	{
		return Crossings;
	}

	const int32 Count = Params.CrossingsPerSide();
	const double Cell = Params.Cell();
	const double Start = -(Count - 1) * Cell / 2.0;

	Crossings.Reserve(Count * Count);
	for (int32 Row = 0; Row < Count; ++Row)
	{
		for (int32 Col = 0; Col < Count; ++Col)
		{
			const FVector Center = Origin + FVector(Start + Col * Cell, Start + Row * Cell, Params.RoadThickness / 2.0);
			FCityPiece& Crossing = Crossings.Add_GetRef(FCityPiece::MakeBox(Center, FVector(Params.StreetWidth, Params.StreetWidth, Params.RoadThickness)));
			Crossing.Col = Col;
			Crossing.Row = Row;
		}
	}
	return Crossings;
}

TArray<FCityPiece> CityLayout::BuildStreets(const FCityParams& Params, TConstArrayView<FCityPiece> Crossings)
{
	int32 Count = 0;
	const TMap<FIntPoint, const FCityPiece*> ByCell = MapByCell(Crossings, Count);

	TArray<FCityPiece> Streets;
	for (const FCityPiece& Crossing : Crossings)
	{
		// Each segment runs from the edge of one crossing to the edge of the next, so they never overlap.
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(0, 1) })
		{
			const FCityPiece* const* Neighbour = ByCell.Find(FIntPoint(Crossing.Col, Crossing.Row) + Step);
			if (!Neighbour)
			{
				continue;
			}

			const FVector Center = (Crossing.Location + (*Neighbour)->Location) / 2.0;
			const double Length = FVector::Dist2D(Crossing.Location, (*Neighbour)->Location) - Params.StreetWidth;
			const FVector Size = Step.X != 0
				? FVector(Length, Params.StreetWidth, Params.RoadThickness)
				: FVector(Params.StreetWidth, Length, Params.RoadThickness);

			FCityPiece& Street = Streets.Add_GetRef(FCityPiece::MakeBox(Center, Size));
			Street.Col = Crossing.Col;
			Street.Row = Crossing.Row;
		}
	}
	return Streets;
}

TArray<FCityPiece> CityLayout::BuildBlocks(const FCityParams& Params, TConstArrayView<FCityPiece> Crossings)
{
	int32 Count = 0;
	const TMap<FIntPoint, const FCityPiece*> ByCell = MapByCell(Crossings, Count);

	TArray<FCityPiece> Blocks;
	for (const FCityPiece& Crossing : Crossings)
	{
		const FCityPiece* const* Diagonal = ByCell.Find(FIntPoint(Crossing.Col + 1, Crossing.Row + 1));
		if (!Diagonal)
		{
			continue;
		}

		// The block fills the gap between the four street edges around it.
		const FVector Delta = (*Diagonal)->Location - Crossing.Location;
		const double GroundZ = Crossing.Location.Z - Params.RoadThickness / 2.0;
		const FVector Center(Crossing.Location.X + Delta.X / 2.0, Crossing.Location.Y + Delta.Y / 2.0, GroundZ + Params.SidewalkHeight / 2.0);
		const FVector Size(FMath::Abs(Delta.X) - Params.StreetWidth, FMath::Abs(Delta.Y) - Params.StreetWidth, Params.SidewalkHeight);

		FCityPiece& Block = Blocks.Add_GetRef(FCityPiece::MakeBox(Center, Size));
		Block.Col = Crossing.Col;
		Block.Row = Crossing.Row;
		Block.BlockID = Crossing.Row * (Count - 1) + Crossing.Col;
		Block.BlockCenter = Center;
	}
	return Blocks;
}

void CityLayout::SplitParks(const FCityParams& Params, TConstArrayView<FCityPiece> Blocks, const int32 Seed, TArray<FCityPiece>& OutBuildable, TArray<FCityPiece>& OutParks)
{
	OutBuildable.Reset();
	OutParks.Reset();

	// Sorting by a seeded value per block and taking the first ones gives exactly the requested share,
	// always the same blocks for the same seed.
	TArray<TPair<float, int32>> Order;
	for (int32 Index = 0; Index < Blocks.Num(); ++Index)
	{
		Order.Emplace(MakeStream(Blocks[Index].Seed, Seed, ParkSalt).FRand(), Index);
	}
	Order.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
	{
		return A.Key != B.Key ? A.Key < B.Key : A.Value < B.Value;
	});

	const int32 ParkCount = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Params.ParkRatio, 0.0, 1.0) * Blocks.Num()), 0, Blocks.Num());
	TBitArray<> IsPark(false, Blocks.Num());
	for (int32 Rank = 0; Rank < ParkCount; ++Rank)
	{
		IsPark[Order[Rank].Value] = true;
	}

	for (int32 Index = 0; Index < Blocks.Num(); ++Index)
	{
		(IsPark[Index] ? OutParks : OutBuildable).Add(Blocks[Index]);
	}
}

void CityLayout::BuildParkVegetation(const FCityParams& Params, TConstArrayView<FCityPiece> Parks, const int32 Seed, TArray<FCityPiece>& OutLawns, TArray<FCityPiece>& OutTrees, TArray<FCityPiece>& OutBushes)
{
	OutLawns.Reset();
	OutTrees.Reset();
	OutBushes.Reset();

	const double Variation = FMath::Clamp(Params.VegetationScaleVariation, 0.0, 1.0);
	const double TreeDensity = FMath::Clamp(Params.ParkTreeDensity, 0.0, 1.0);

	for (const FCityPiece& Park : Parks)
	{
		// The park fills the inside of the block; the sidewalk ring stays for the street props.
		const FBox ParkBox = Park.GetWorldBox();
		const FVector2D Inner = FVector2D(ParkBox.GetSize()) - FVector2D(2.0 * Params.SidewalkWidth);
		if (Inner.X <= 0.0 || Inner.Y <= 0.0)
		{
			continue;
		}

		const FVector2D Center(ParkBox.GetCenter());
		FCityPiece& Lawn = OutLawns.Add_GetRef(FCityPiece::MakeBox(FVector(Center, ParkBox.Max.Z + Params.LawnThickness / 2.0), FVector(Inner, Params.LawnThickness)));
		Lawn.BlockID = Park.BlockID;

		const double GroundZ = ParkBox.Max.Z + Params.LawnThickness;
		const FIntPoint Cells(
			FMath::Max(1, FMath::FloorToInt(Inner.X / Params.ParkCellSize)),
			FMath::Max(1, FMath::FloorToInt(Inner.Y / Params.ParkCellSize)));
		const FVector2D CellSize(Inner.X / Cells.X, Inner.Y / Cells.Y);
		const FVector2D Corner = Center - Inner / 2.0;

		for (int32 J = 0; J < Cells.Y; ++J)
		{
			for (int32 I = 0; I < Cells.X; ++I)
			{
				const FVector CellCenter(Corner + FVector2D(I + 0.5, J + 0.5) * CellSize, GroundZ);
				FRandomStream Random = MakeStream(SeedFromPosition(CellCenter), Seed, VegetationSalt);

				const bool bTree = Random.FRand() < TreeDensity;
				const bool bBush = !bTree && Random.FRand() < Params.ParkBushChance;
				if (!bTree && !bBush)
				{
					continue;
				}

				const double ScaleFactor = 1.0 + Variation * Random.FRandRange(-0.5, 0.5);
				const double Yaw = Random.FRandRange(0.0, 360.0);
				FCityPiece Plant = bTree
					? MakePlant(CellCenter, Params.ParkTreeScale * ScaleFactor, Yaw, Params.ParkTreeCrownLocal, Params.ParkTreeHeightLocal)
					: MakePlant(CellCenter, Params.ParkBushScale * ScaleFactor, Yaw, Params.ParkBushSizeLocal, Params.ParkBushSizeLocal);

				// Move it inside its own cell as far as its footprint allows, so plants never overlap.
				const FVector2D Margin = ((CellSize - Plant.GetFootprint().GetSize()) / 2.0).ComponentMax(FVector2D::ZeroVector);
				Plant.Location += FVector(Random.FRandRange(-1.0, 1.0) * Margin.X, Random.FRandRange(-1.0, 1.0) * Margin.Y, 0.0);
				Plant.Seed = SeedFromPosition(Plant.Location);
				Plant.BlockID = Park.BlockID;
				(bTree ? OutTrees : OutBushes).Add(Plant);
			}
		}
	}
}

TArray<FCityPiece> CityLayout::BuildLots(const FCityParams& Params, TConstArrayView<FCityPiece> Blocks)
{
	TArray<FCityPiece> Lots;
	for (const FCityPiece& Block : Blocks)
	{
		const FBox BlockBox = Block.GetWorldBox();
		const FVector2D Inner = FVector2D(BlockBox.GetSize()) - FVector2D(2.0 * Params.SidewalkWidth);
		if (Inner.X <= 0.0 || Inner.Y <= 0.0)
		{
			continue;
		}

		const FIntPoint PerSide(
			FMath::Max(1, FMath::RoundToInt(Inner.X / Params.LotSize)),
			FMath::Max(1, FMath::RoundToInt(Inner.Y / Params.LotSize)));
		const FVector2D LotSize(Inner.X / PerSide.X, Inner.Y / PerSide.Y);
		const FVector2D Corner = FVector2D(BlockBox.GetCenter()) - Inner / 2.0;
		const double TopZ = BlockBox.Max.Z;

		for (int32 J = 0; J < PerSide.Y; ++J)
		{
			for (int32 I = 0; I < PerSide.X; ++I)
			{
				const FVector2D LotCenter = Corner + FVector2D(I + 0.5, J + 0.5) * LotSize;
				FCityPiece& Lot = Lots.Add_GetRef(FCityPiece::MakeBox(FVector(LotCenter, TopZ), FVector(LotSize, 1.0)));
				Lot.BlockID = Block.BlockID;
				Lot.BlockCenter = FVector(FVector2D(BlockBox.GetCenter()), TopZ);
				Lot.bFrontage = I == 0 || J == 0 || I == PerSide.X - 1 || J == PerSide.Y - 1;
			}
		}
	}
	return Lots;
}

TArray<FCityPiece> CityLayout::PickBuiltLots(const FCityParams& Params, TConstArrayView<FCityPiece> Lots, const int32 Seed)
{
	TArray<FCityPiece> Built;
	for (const FCityPiece& Lot : Lots)
	{
		if (!Lot.bFrontage)
		{
			continue;
		}

		// Each lot draws its own value, so changing one lot never changes the others.
		FCityPiece Candidate = Lot;
		Candidate.Density = MakeStream(Lot.Seed, Seed, DensitySalt).FRand();
		if (Candidate.Density < Params.BuildingDensity)
		{
			Built.Add(Candidate);
		}
	}
	return Built;
}

TArray<FCityPiece> CityLayout::BuildBuildings(const FCityParams& Params, TConstArrayView<FCityPiece> BuiltLots, const int32 Seed)
{
	const double Variation = FMath::Clamp(Params.BuildingVariation, 0.0, 1.0);

	TArray<FCityPiece> Buildings;
	Buildings.Reserve(BuiltLots.Num());
	for (const FCityPiece& Lot : BuiltLots)
	{
		FRandomStream Random = MakeStream(Lot.Seed, Seed, BuildingSalt);

		const double Spread = Variation * Random.FRandRange(-0.75, 0.75);
		const double Height = FMath::Max(Params.MinBuildingHeight, Params.BuildingHeight * (1.0 + Spread));

		// The footprint is always smaller than the lot, so it never reaches the sidewalk.
		const FVector LotSize = GetSize(Lot);
		const double LotSide = FMath::Min(LotSize.X, LotSize.Y);
		const double Side = LotSide * (0.9 - 0.3 * Variation * Random.FRand());

		const double Margin = (LotSide - Side) / 2.0;
		const FVector2D Offset(Random.FRandRange(-1.0, 1.0) * Margin * Variation, Random.FRandRange(-1.0, 1.0) * Margin * Variation);

		// Face the street: away from the block center, snapped to 90 degrees.
		const FVector2D Away = FVector2D(Lot.Location - Lot.BlockCenter);
		const double Yaw = Away.IsNearlyZero() ? 0.0 : FMath::GridSnap(FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X)), 90.0);

		const FVector Center(FVector2D(Lot.Location) + Offset, Lot.Location.Z + Height / 2.0);
		FCityPiece& Building = Buildings.Add_GetRef(FCityPiece::MakeBox(Center, FVector(Side, Side, Height), Yaw));
		Building.BlockID = Lot.BlockID;
		Building.BlockCenter = Lot.BlockCenter;
		Building.Height = Height;
		Building.Density = Lot.Density;
	}
	return Buildings;
}

void CityLayout::BuildProps(const FCityParams& Params, TConstArrayView<FCityPiece> Blocks, TConstArrayView<FCityPiece> Crossings, const int32 Seed, TArray<FCityPiece>& OutLamps, TArray<FCityPiece>& OutTrees)
{
	OutLamps.Reset();
	OutTrees.Reset();

	struct FSide
	{
		FVector2D Normal;
		FVector2D Along;
	};
	const FSide Sides[] = {
		{ FVector2D(0.0, 1.0), FVector2D(1.0, 0.0) },
		{ FVector2D(1.0, 0.0), FVector2D(0.0, 1.0) },
		{ FVector2D(0.0, -1.0), FVector2D(1.0, 0.0) },
		{ FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0) },
	};

	const double TreeScale = FMath::Max(Params.TreeScale, 0.01);
	for (const FCityPiece& Block : Blocks)
	{
		const FBox BlockBox = Block.GetWorldBox();
		const FVector2D Center(BlockBox.GetCenter());
		const FVector2D HalfSize(BlockBox.GetExtent());
		const double TopZ = BlockBox.Max.Z;

		for (const FSide& Side : Sides)
		{
			// The middle of the sidewalk: block edge, half a sidewalk inwards.
			const double Offset = FMath::Abs(Side.Normal.X) * HalfSize.X + FMath::Abs(Side.Normal.Y) * HalfSize.Y - Params.SidewalkWidth / 2.0;
			const double Length = 2.0 * (FMath::Abs(Side.Along.X) * HalfSize.X + FMath::Abs(Side.Along.Y) * HalfSize.Y);
			const int32 Count = FMath::FloorToInt(Length / Params.PropSpacing);
			const double Start = -(Count - 1) * Params.PropSpacing / 2.0;
			const double FaceStreetYaw = FMath::RadiansToDegrees(FMath::Atan2(Side.Normal.Y, Side.Normal.X));

			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector2D Point = Center + Side.Normal * Offset + Side.Along * (Start + Index * Params.PropSpacing);

				// Keep the corners free around crossings.
				const bool bNearCrossing = Crossings.ContainsByPredicate([&Point, &Params](const FCityPiece& Crossing)
				{
					return FVector2D::Distance(FVector2D(Crossing.Location), Point) < Params.StreetWidth;
				});
				if (bNearCrossing)
				{
					continue;
				}

				if (Index % 2 == 0)
				{
					FCityPiece& Lamp = OutLamps.Add_GetRef(FCityPiece::MakeBox(
						FVector(Point, TopZ + Params.LampHeight / 2.0),
						FVector(2.0 * Params.LampRadius, 2.0 * Params.LampRadius, Params.LampHeight),
						FaceStreetYaw));
					Lamp.BlockID = Block.BlockID;
				}
				else
				{
					FCityPiece Tree;
					Tree.Location = FVector(Point, TopZ);
					Tree.Seed = SeedFromPosition(Tree.Location);
					FRandomStream Random = MakeStream(Tree.Seed, Seed, PropSalt);
					Tree.Yaw = Random.FRandRange(0.0, 360.0);
					const double Scale = TreeScale * Random.FRandRange(0.8, 1.2);
					Tree.Scale = FVector(Scale);

					// Pivot at the base of the trunk; the box stands for the crown, in mesh units. It is shrunk by
					// sqrt(2) so the random yaw never makes its world box wider than TreeCrown.
					const double HalfCrown = Params.TreeCrown / 2.0 / UE_SQRT_2 / Scale;
					Tree.LocalBounds = FBox(FVector(-HalfCrown, -HalfCrown, 0.0), FVector(HalfCrown, HalfCrown, 2.0 * HalfCrown));
					Tree.BlockID = Block.BlockID;
					OutTrees.Add(Tree);
				}
			}
		}
	}
}

FCityOverlapReport CityLayout::CleanOverlaps(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TConstArrayView<FCityPiece> Crossings, TArray<FCityPiece>& InOutBuildings, TArray<FCityPiece>& InOutLamps, TArray<FCityPiece>& InOutTrees)
{
	FCityOverlapReport Report;
	const double GridCell = FMath::Max(Params.Cell(), 100.0);

	// Nothing may stand on the road.
	FFootprintGrid RoadArea(GridCell);
	for (const FCityPiece& Piece : Streets)
	{
		RoadArea.Add(Piece.GetFootprint());
	}
	for (const FCityPiece& Piece : Crossings)
	{
		RoadArea.Add(Piece.GetFootprint());
	}

	// Larger buildings win when two overlap.
	TArray<int32> Order;
	for (int32 Index = 0; Index < InOutBuildings.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Order.StableSort([&InOutBuildings](const int32 A, const int32 B)
	{
		return InOutBuildings[A].GetFootprint().GetArea() > InOutBuildings[B].GetFootprint().GetArea();
	});

	FFootprintGrid KeptBuildings(GridCell);
	TBitArray<> Keep(false, InOutBuildings.Num());
	for (const int32 Index : Order)
	{
		const FBox2D Footprint = InOutBuildings[Index].GetFootprint();
		if (!RoadArea.OverlapsAny(Footprint) && !KeptBuildings.OverlapsAny(Footprint))
		{
			KeptBuildings.Add(Footprint);
			Keep[Index] = true;
		}
	}

	TArray<FCityPiece> Buildings;
	for (int32 Index = 0; Index < InOutBuildings.Num(); ++Index)
	{
		if (Keep[Index])
		{
			Buildings.Add(InOutBuildings[Index]);
		}
	}
	Report.RemovedBuildings = InOutBuildings.Num() - Buildings.Num();
	InOutBuildings = MoveTemp(Buildings);

	// Props may not stand on the road or inside a building.
	const auto RemoveBlockedProps = [&RoadArea, &KeptBuildings](TArray<FCityPiece>& Props)
	{
		return Props.RemoveAll([&RoadArea, &KeptBuildings](const FCityPiece& Prop)
		{
			const FBox2D Footprint = Prop.GetFootprint();
			return RoadArea.OverlapsAny(Footprint) || KeptBuildings.OverlapsAny(Footprint);
		});
	};
	Report.RemovedLamps = RemoveBlockedProps(InOutLamps);
	Report.RemovedTrees = RemoveBlockedProps(InOutTrees);

	// Check every layer before spawning meshes.
	const int32 Count = Params.CrossingsPerSide();
	if (Crossings.Num() != Count * Count)
	{
		Report.FailedChecks.Add(FString::Printf(TEXT("Expected %d crossings but got %d."), Count * Count, Crossings.Num()));
	}
	if (Params.BuildingDensity > 0.0 && Params.ParkRatio < 1.0 && InOutBuildings.IsEmpty())
	{
		Report.FailedChecks.Add(TEXT("BuildingDensity is above 0 but no buildings were generated."));
	}
	if (InOutLamps.IsEmpty() && InOutTrees.IsEmpty())
	{
		Report.FailedChecks.Add(TEXT("No street props were generated."));
	}
	return Report;
}

void CityLayout::BuildCrosswalks(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TArray<FCityPiece>& OutZones, TArray<FCityPiece>& OutStripes)
{
	OutZones.Reset();
	OutStripes.Reset();

	for (const FCityPiece& Street : Streets)
	{
		const FStreetFrame Frame(Street);
		if (Frame.Length < 2.0 * Params.CrosswalkLength)
		{
			continue;
		}

		// One crosswalk at each end of the segment, right next to the crossing.
		const double MarkingZ = Frame.TopZ + Params.MarkingThickness / 2.0;
		const int32 StripeCount = FMath::Max(1, FMath::FloorToInt(Frame.Width / Params.CrosswalkStripePitch));
		const double FirstStripe = -(StripeCount - 1) * Params.CrosswalkStripePitch / 2.0;
		for (const double End : { -1.0, 1.0 })
		{
			const double Along = End * (Frame.Length - Params.CrosswalkLength) / 2.0;
			OutZones.Add(Frame.MakeBox(Along, 0.0, Params.CrosswalkLength, Frame.Width, MarkingZ, Params.MarkingThickness));

			// Zebra stripes run with the traffic and repeat across the street.
			for (int32 Stripe = 0; Stripe < StripeCount; ++Stripe)
			{
				OutStripes.Add(Frame.MakeBox(Along, FirstStripe + Stripe * Params.CrosswalkStripePitch,
					0.9 * Params.CrosswalkLength, Params.CrosswalkStripeWidth, MarkingZ, Params.MarkingThickness));
			}
		}
	}
}

TArray<FCityPiece> CityLayout::BuildEntrances(const FCityParams& Params, TConstArrayView<FCityPiece> Buildings, TConstArrayView<FCityPiece> Blocks)
{
	TMap<int32, FBox> BlockBoxes;
	for (const FCityPiece& Block : Blocks)
	{
		BlockBoxes.Add(Block.BlockID, Block.GetWorldBox());
	}

	TArray<FCityPiece> Entrances;
	for (const FCityPiece& Building : Buildings)
	{
		const FBox* BlockBox = BlockBoxes.Find(Building.BlockID);
		const double YawRadians = FMath::DegreesToRadians(Building.Yaw);
		const FVector2D Facing(FMath::RoundToDouble(FMath::Cos(YawRadians)), FMath::RoundToDouble(FMath::Sin(YawRadians)));
		if (!BlockBox || Facing.IsNearlyZero())
		{
			continue;
		}

		// The entrance sits in the curb lane of the street the building faces, in line with the building.
		const FVector2D BlockCenter(BlockBox->GetCenter());
		const FVector2D HalfSize(BlockBox->GetExtent());
		const bool bFacesX = FMath::Abs(Facing.X) > 0.5;
		const FVector2D Center = bFacesX
			? FVector2D(BlockCenter.X + Facing.X * (HalfSize.X + Params.ParkingLaneWidth / 2.0), Building.Location.Y)
			: FVector2D(Building.Location.X, BlockCenter.Y + Facing.Y * (HalfSize.Y + Params.ParkingLaneWidth / 2.0));
		const FVector2D Size = bFacesX ? FVector2D(Params.ParkingLaneWidth, Params.EntranceWidth) : FVector2D(Params.EntranceWidth, Params.ParkingLaneWidth);
		const double RoadTopZ = BlockBox->Min.Z + Params.RoadThickness;

		FCityPiece& Entrance = Entrances.Add_GetRef(FCityPiece::MakeBox(FVector(Center, RoadTopZ + Params.MarkingThickness / 2.0), FVector(Size, Params.MarkingThickness)));
		Entrance.BlockID = Building.BlockID;
	}
	return Entrances;
}

void CityLayout::BuildParkedCars(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TConstArrayView<FCityPiece> Crossings, TConstArrayView<FCityPiece> Blocks, TConstArrayView<FCityPiece> Crosswalks, TConstArrayView<FCityPiece> Entrances, const int32 Seed, TArray<FCityPiece>& OutCars, TArray<FCityPiece>& OutParts)
{
	OutCars.Reset();
	OutParts.Reset();

	const double Variation = FMath::Clamp(Params.CarVariation, 0.0, 1.0);
	const double Occupancy = FMath::Clamp(Params.ParkingOccupancy, 0.0, 1.0);
	const double Clearance = FMath::Max(Params.ParkingClearance, 0.0);
	const int32 ModelCount = 1 + FMath::RoundToInt32(Variation * (UE_ARRAY_COUNT(CarModels) - 1));
	const int32 ColorCount = 1 + FMath::RoundToInt32(Variation * (UE_ARRAY_COUNT(CarPalette) - 1));
	const double GridCell = FMath::Max(Params.Cell(), 100.0);

	// Forbidden zones: cars must stay Clearance away from these...
	FFootprintGrid KeepClear(GridCell);
	for (const FCityPiece& Piece : Crossings)
	{
		KeepClear.Add(Piece.GetFootprint());
	}
	for (const FCityPiece& Piece : Crosswalks)
	{
		KeepClear.Add(Piece.GetFootprint());
	}

	// ...and may not block an entrance.
	FFootprintGrid EntranceArea(GridCell);
	for (const FCityPiece& Piece : Entrances)
	{
		EntranceArea.Add(Piece.GetFootprint());
	}

	FFootprintGrid Curbs(GridCell);
	for (const FCityPiece& Block : Blocks)
	{
		Curbs.Add(Block.GetFootprint());
	}

	for (const FCityPiece& Street : Streets)
	{
		const FStreetFrame Frame(Street);
		const int32 SlotCount = FMath::FloorToInt(Frame.Length / Params.CarSlotLength);
		const double FirstSlot = -(SlotCount - 1) * Params.CarSlotLength / 2.0;

		for (const double Side : { -1.0, 1.0 })
		{
			// Only park along a curb: the edge of the city has streets with no block on the outer side.
			const FCityPiece Probe = Frame.MakeBox(0.0, Side * (Frame.Width + Params.SidewalkWidth) / 2.0, 20.0, 20.0, Frame.TopZ, 1.0);
			if (!Curbs.OverlapsAny(Probe.GetFootprint()))
			{
				continue;
			}

			// Right-hand traffic: cars on the right of the axis face along it, cars on the left face back.
			const double LaneOffset = Side * (Frame.Width - Params.ParkingLaneWidth) / 2.0;
			const double Yaw = Frame.AxisYaw + (Side > 0.0 ? 180.0 : 0.0);
			const FVector2D Forward = Frame.Axis * (Side > 0.0 ? -1.0 : 1.0);
			const FVector2D Across(-Forward.Y, Forward.X);

			for (int32 Slot = 0; Slot < SlotCount; ++Slot)
			{
				const double Along = FirstSlot + Slot * Params.CarSlotLength;
				const FCityPiece SlotBox = Frame.MakeBox(Along, LaneOffset, Params.CarSlotLength, Params.ParkingLaneWidth, Frame.TopZ, 1.0);
				const FCityPiece ClearedBox = Frame.MakeBox(Along, LaneOffset, Params.CarSlotLength + 2.0 * Clearance, Params.ParkingLaneWidth, Frame.TopZ, 1.0);
				if (KeepClear.OverlapsAny(ClearedBox.GetFootprint()) || EntranceArea.OverlapsAny(SlotBox.GetFootprint()))
				{
					continue;
				}

				FRandomStream Random = MakeStream(SeedFromPosition(SlotBox.Location), Seed, CarSalt);
				if (Random.FRand() >= Occupancy)
				{
					continue;
				}

				const int32 ModelIndex = Random.RandRange(0, ModelCount - 1);
				const int32 ColorIndex = Random.RandRange(0, ColorCount - 1);
				const FCarModel& Model = CarModels[ModelIndex];

				// A little play inside the slot so the row of cars is not perfectly regular.
				const double Jitter = Random.FRandRange(-1.0, 1.0) * (Params.CarSlotLength - Model.Body.X) / 4.0;
				const FVector2D Center = FVector2D(SlotBox.Location) + Frame.Axis * Jitter;
				const double BodyBottom = Frame.TopZ + Params.CarLift;

				const auto AddPart = [&](TArray<FCityPiece>& Target, const FVector2D& PartCenter, const double CenterZ, const FVector& Size, const int32 PartColor)
				{
					FCityPiece& Part = Target.Add_GetRef(FCityPiece::MakeBox(FVector(PartCenter, CenterZ), Size, Yaw));
					Part.ModelIndex = ModelIndex;
					Part.ColorIndex = PartColor;
					Part.Look = PartColor == INDEX_NONE ? TireLook : GetCarLook(PartColor);
				};
				AddPart(OutCars, Center, BodyBottom + Model.Body.Z / 2.0, Model.Body, ColorIndex);
				AddPart(OutParts, Center + Forward * Model.CabinOffset, BodyBottom + Model.Body.Z + Model.Cabin.Z / 2.0, Model.Cabin, ColorIndex);

				const FVector WheelBox(Params.WheelSize, Params.WheelWidth, Params.WheelSize);
				for (const double Front : { -1.0, 1.0 })
				{
					for (const double Lateral : { -1.0, 1.0 })
					{
						const FVector2D WheelCenter = Center
							+ Forward * (Front * (Model.Body.X / 2.0 - Params.WheelSize))
							+ Across * (Lateral * (Model.Body.Y - Params.WheelWidth) / 2.0);
						AddPart(OutParts, WheelCenter, Frame.TopZ + Params.WheelSize / 2.0, WheelBox, INDEX_NONE);
					}
				}
			}
		}
	}
}

TConstArrayView<FLinearColor> CityLayout::GetCarPalette()
{
	return CarPalette;
}

int32 CityLayout::GetCarModelCount()
{
	return UE_ARRAY_COUNT(CarModels);
}

TConstArrayView<TPair<FName, FLinearColor>> CityLayout::GetLooks()
{
	static const TArray<TPair<FName, FLinearColor>> Looks = []()
	{
		TArray<TPair<FName, FLinearColor>> Result;
		for (int32 ColorIndex = 0; ColorIndex < UE_ARRAY_COUNT(CarPalette); ++ColorIndex)
		{
			Result.Emplace(GetCarLook(ColorIndex), CarPalette[ColorIndex]);
		}
		Result.Emplace(TireLook, FLinearColor(0.01f, 0.01f, 0.01f));
		Result.Emplace(SignPoleLook, FLinearColor(0.3f, 0.3f, 0.32f));
		Result.Emplace(SignRedLook, FLinearColor(0.6f, 0.02f, 0.02f));
		Result.Emplace(SignWhiteLook, FLinearColor(0.9f, 0.9f, 0.9f));
		Result.Emplace(SignBlueLook, FLinearColor(0.02f, 0.12f, 0.6f));
		return Result;
	}();
	return Looks;
}

bool CityLayout::IsMajorStreet(const FCityParams& Params, const int32 RowOrCol)
{
	return Params.MajorStreetEvery > 0 && RowOrCol % Params.MajorStreetEvery == 0;
}

namespace
{
	// A sign is a pole plus the panels that give it its shape, all facing the traffic.
	void AddSignPieces(const FCityParams& Params, const ECitySignType Type, const FVector2D& Position, const FVector2D& Facing, const double GroundZ, TArray<FCityPiece>& OutSigns, TArray<FCityPiece>& OutParts)
	{
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Facing.Y, Facing.X));
		FCityPiece& Pole = OutSigns.Add_GetRef(FCityPiece::MakeBox(
			FVector(Position, GroundZ + Params.SignPoleHeight / 2.0),
			FVector(2.0 * Params.SignPoleRadius, 2.0 * Params.SignPoleRadius, Params.SignPoleHeight), Yaw));
		Pole.Shape = ECityShape::Cylinder;
		Pole.Look = SignPoleLook;
		Pole.SignType = static_cast<int32>(Type);

		// Panels are thin along the facing direction; Forward stacks the inner panel in front of the outer one.
		const double PanelZ = GroundZ + Params.SignPoleHeight - 35.0;
		const auto AddPanel = [&](const ECityShape Shape, const FName Look, const FVector& Size, const double Forward, const double Pitch, const double Roll, const double ZOffset)
		{
			FCityPiece& Panel = OutParts.Add_GetRef(FCityPiece::MakeBox(
				FVector(Position + Facing * (Params.SignPoleRadius + Forward), PanelZ + ZOffset), Size, Yaw));
			Panel.Pitch = Pitch;
			Panel.Roll = Roll;
			Panel.Shape = Shape;
			Panel.Look = Look;
			Panel.SignType = static_cast<int32>(Type);
		};

		// Discs are cylinders pitched 90 degrees; triangles are flattened cones rolled upside down.
		switch (Type)
		{
		case ECitySignType::Stop:
			AddPanel(ECityShape::Cylinder, SignRedLook, FVector(70.0, 70.0, 2.0), 2.0, 90.0, 0.0, 0.0);
			break;
		case ECitySignType::Yield:
			AddPanel(ECityShape::Cone, SignRedLook, FVector(2.0, 80.0, 70.0), 2.0, 0.0, 180.0, 0.0);
			AddPanel(ECityShape::Cone, SignWhiteLook, FVector(2.0, 50.0, 44.0), 3.5, 0.0, 180.0, 4.4);
			break;
		case ECitySignType::Direction:
			AddPanel(ECityShape::Cube, SignBlueLook, FVector(2.0, 60.0, 60.0), 2.0, 0.0, 0.0, 0.0);
			AddPanel(ECityShape::Cube, SignWhiteLook, FVector(2.0, 8.0, 30.0), 3.5, 0.0, 0.0, -6.0);
			AddPanel(ECityShape::Cube, SignWhiteLook, FVector(2.0, 18.0, 18.0), 3.5, 0.0, 45.0, 10.0);
			break;
		case ECitySignType::SpeedLimit:
			AddPanel(ECityShape::Cylinder, SignRedLook, FVector(60.0, 60.0, 2.0), 2.0, 90.0, 0.0, 0.0);
			AddPanel(ECityShape::Cylinder, SignWhiteLook, FVector(46.0, 46.0, 2.0), 3.5, 90.0, 0.0, 0.0);
			break;
		}
	}
}

void CityLayout::BuildSigns(const FCityParams& Params, TConstArrayView<FCityPiece> Streets, TConstArrayView<FCityPiece> Crossings, TConstArrayView<FCityPiece> Blocks, TConstArrayView<FCityPiece> Lamps, TConstArrayView<FCityPiece> Trees, TArray<FCityPiece>& OutSigns, TArray<FCityPiece>& OutParts)
{
	OutSigns.Reset();
	OutParts.Reset();

	const double GridCell = FMath::Max(Params.Cell(), 100.0);
	FFootprintGrid Sidewalks(GridCell);
	for (const FCityPiece& Block : Blocks)
	{
		Sidewalks.Add(Block.GetFootprint());
	}

	FFootprintGrid Road(GridCell);
	TMap<FIntPoint, const FCityPiece*> CrossingsByPosition;
	for (const FCityPiece& Crossing : Crossings)
	{
		Road.Add(Crossing.GetFootprint());
		CrossingsByPosition.Add(FIntPoint(FMath::RoundToInt32(Crossing.Location.X), FMath::RoundToInt32(Crossing.Location.Y)), &Crossing);
	}
	for (const FCityPiece& Street : Streets)
	{
		Road.Add(Street.GetFootprint());
	}

	// Lamp posts and tree trunks the sign poles must keep away from.
	FFootprintGrid Obstacles(GridCell);
	for (TConstArrayView<FCityPiece> Props : { Lamps, Trees })
	{
		for (const FCityPiece& Prop : Props)
		{
			const FVector2D Trunk(Prop.Location);
			Obstacles.Add(FBox2D(Trunk - FVector2D(Params.SignPropClearance), Trunk + FVector2D(Params.SignPropClearance)));
		}
	}

	const auto FindCrossing = [&CrossingsByPosition](const FVector2D& Position) -> const FCityPiece*
	{
		const FIntPoint Rounded(FMath::RoundToInt32(Position.X), FMath::RoundToInt32(Position.Y));
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				if (const FCityPiece* const* Crossing = CrossingsByPosition.Find(Rounded + FIntPoint(DX, DY)))
				{
					return *Crossing;
				}
			}
		}
		return nullptr;
	};

	// Slides the sign back along the curb until it is clear of lamps and trees; gives up if it would leave the sidewalk.
	const auto PlaceSign = [&](const ECitySignType Type, const FVector2D& Base, const FVector2D& Facing, const double GroundZ)
	{
		for (int32 Step = 0; Step <= 8; ++Step)
		{
			const FVector2D Position = Base + Facing * (Step * 50.0);
			const FBox2D Pole(Position - FVector2D(Params.SignPoleRadius), Position + FVector2D(Params.SignPoleRadius));
			if (!Sidewalks.OverlapsAny(Pole) || Road.OverlapsAny(Pole))
			{
				return;
			}
			if (!Obstacles.OverlapsAny(Pole))
			{
				AddSignPieces(Params, Type, Position, Facing, GroundZ, OutSigns, OutParts);
				return;
			}
		}
	};

	const double Distance = FMath::Max(Params.SignDistance, 4.0 * Params.SignPoleRadius);
	const double CurbOffset = Params.StreetWidth / 2.0 + Params.SignCurbOffset;

	for (const FCityPiece& Street : Streets)
	{
		const FStreetFrame Frame(Street);
		const bool bAlongX = Frame.Axis.X != 0.0;
		const double GroundZ = Frame.TopZ - Params.RoadThickness + Params.SidewalkHeight;

		for (const double End : { -1.0, 1.0 })
		{
			// Traffic driving towards this end of the segment, with the sidewalk on its right.
			const FVector2D Drive = Frame.Axis * End;
			const FVector2D Right(Drive.Y, -Drive.X);
			const FCityPiece* Crossing = FindCrossing(Frame.Center + Drive * (Frame.Length + Params.StreetWidth) / 2.0);
			if (!Crossing)
			{
				continue;
			}

			const bool bOwnMajor = IsMajorStreet(Params, bAlongX ? Crossing->Row : Crossing->Col);
			const bool bCrossMajor = IsMajorStreet(Params, bAlongX ? Crossing->Col : Crossing->Row);

			// Priority: a minor street stops at a main street; between equal streets the ones along Y yield.
			// If the preferred type is disabled, the other one is used.
			TOptional<ECitySignType> Type;
			if (!bOwnMajor && bCrossMajor)
			{
				Type = Params.bStopSigns ? ECitySignType::Stop : ECitySignType::Yield;
			}
			else if (bOwnMajor == bCrossMajor && !bAlongX)
			{
				Type = Params.bYieldSigns ? ECitySignType::Yield : ECitySignType::Stop;
			}
			if (Type && ((*Type == ECitySignType::Stop && !Params.bStopSigns) || (*Type == ECitySignType::Yield && !Params.bYieldSigns)))
			{
				Type.Reset();
			}

			const FVector2D Base = Frame.Center + Drive * FMath::Max(Frame.Length / 2.0 - Distance, 0.0) + Right * CurbOffset;
			if (Type)
			{
				PlaceSign(*Type, Base, -Drive, GroundZ);
			}
			if (Params.bDirectionSigns && bOwnMajor && bCrossMajor)
			{
				PlaceSign(ECitySignType::Direction, Base - Drive * Params.DirectionSignSetback, -Drive, GroundZ);
			}
		}

		// Speed limits along the segment for both directions, away from the intersection approaches.
		if (!Params.bSpeedLimitSigns || Params.SignSpacing <= 0.0)
		{
			continue;
		}

		// Spread them every SignSpacing over the middle of the segment, centered, leaving the approaches free.
		const double Reach = Frame.Length / 2.0 - Distance - Params.DirectionSignSetback - 200.0;
		if (Reach < 0.0)
		{
			continue;
		}

		const int32 Count = FMath::FloorToInt(2.0 * Reach / Params.SignSpacing) + 1;
		const double First = -(Count - 1) * Params.SignSpacing / 2.0;
		for (const double Direction : { -1.0, 1.0 })
		{
			const FVector2D Drive = Frame.Axis * Direction;
			const FVector2D Right(Drive.Y, -Drive.X);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const double Along = First + Index * Params.SignSpacing;
				PlaceSign(ECitySignType::SpeedLimit, Frame.Center + Frame.Axis * Along + Right * CurbOffset, -Drive, GroundZ);
			}
		}
	}
}

FCityLayout CityLayout::Generate(const FCityParams& Params, const FVector& Origin, const int32 Seed, FCityOverlapReport* OutReport)
{
	FCityLayout Layout;
	Layout.Crossings = BuildCrossings(Params, Origin);
	Layout.Streets = BuildStreets(Params, Layout.Crossings);
	Layout.Blocks = BuildBlocks(Params, Layout.Crossings);
	SplitParks(Params, Layout.Blocks, Seed, Layout.BuildableBlocks, Layout.Parks);
	BuildParkVegetation(Params, Layout.Parks, Seed, Layout.Lawns, Layout.ParkTrees, Layout.ParkBushes);
	Layout.Lots = BuildLots(Params, Layout.BuildableBlocks);
	Layout.Built = PickBuiltLots(Params, Layout.Lots, Seed);
	Layout.Buildings = BuildBuildings(Params, Layout.Built, Seed);
	BuildProps(Params, Layout.Blocks, Layout.Crossings, Seed, Layout.Lamps, Layout.Trees);

	const FCityOverlapReport Report = CleanOverlaps(Params, Layout.Streets, Layout.Crossings, Layout.Buildings, Layout.Lamps, Layout.Trees);

	BuildCrosswalks(Params, Layout.Streets, Layout.Crosswalks, Layout.CrosswalkStripes);
	Layout.Entrances = BuildEntrances(Params, Layout.Buildings, Layout.Blocks);
	BuildParkedCars(Params, Layout.Streets, Layout.Crossings, Layout.Blocks, Layout.Crosswalks, Layout.Entrances, Seed, Layout.Cars, Layout.CarParts);
	BuildSigns(Params, Layout.Streets, Layout.Crossings, Layout.Blocks, Layout.Lamps, Layout.Trees, Layout.Signs, Layout.SignParts);
	if (OutReport)
	{
		*OutReport = Report;
	}
	return Layout;
}

bool CityLayout::AreEqual(const FCityLayout& A, const FCityLayout& B)
{
	return ArePiecesEqual(A.Crossings, B.Crossings) && ArePiecesEqual(A.Streets, B.Streets)
		&& ArePiecesEqual(A.Blocks, B.Blocks) && ArePiecesEqual(A.Parks, B.Parks)
		&& ArePiecesEqual(A.Lawns, B.Lawns) && ArePiecesEqual(A.ParkTrees, B.ParkTrees)
		&& ArePiecesEqual(A.ParkBushes, B.ParkBushes) && ArePiecesEqual(A.Lots, B.Lots)
		&& ArePiecesEqual(A.Built, B.Built) && ArePiecesEqual(A.Buildings, B.Buildings)
		&& ArePiecesEqual(A.Lamps, B.Lamps) && ArePiecesEqual(A.Trees, B.Trees)
		&& ArePiecesEqual(A.Crosswalks, B.Crosswalks) && ArePiecesEqual(A.Entrances, B.Entrances)
		&& ArePiecesEqual(A.Cars, B.Cars) && ArePiecesEqual(A.CarParts, B.CarParts)
		&& ArePiecesEqual(A.Signs, B.Signs) && ArePiecesEqual(A.SignParts, B.SignParts);
}
