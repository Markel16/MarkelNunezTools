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

	FRandomStream MakeStream(const int32 PieceSeed, const int32 GraphSeed, const uint32 Salt)
	{
		return FRandomStream(static_cast<int32>(HashCombine(HashCombine(static_cast<uint32>(PieceSeed), static_cast<uint32>(GraphSeed)), Salt)));
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
	return FTransform(FRotator(0.0, Yaw, 0.0), Location, Scale);
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
	if (Params.BuildingDensity > 0.0 && InOutBuildings.IsEmpty())
	{
		Report.FailedChecks.Add(TEXT("BuildingDensity is above 0 but no buildings were generated."));
	}
	if (InOutLamps.IsEmpty() && InOutTrees.IsEmpty())
	{
		Report.FailedChecks.Add(TEXT("No street props were generated."));
	}
	return Report;
}

FCityLayout CityLayout::Generate(const FCityParams& Params, const FVector& Origin, const int32 Seed, FCityOverlapReport* OutReport)
{
	FCityLayout Layout;
	Layout.Crossings = BuildCrossings(Params, Origin);
	Layout.Streets = BuildStreets(Params, Layout.Crossings);
	Layout.Blocks = BuildBlocks(Params, Layout.Crossings);
	Layout.Lots = BuildLots(Params, Layout.Blocks);
	Layout.Built = PickBuiltLots(Params, Layout.Lots, Seed);
	Layout.Buildings = BuildBuildings(Params, Layout.Built, Seed);
	BuildProps(Params, Layout.Blocks, Layout.Crossings, Seed, Layout.Lamps, Layout.Trees);

	const FCityOverlapReport Report = CleanOverlaps(Params, Layout.Streets, Layout.Crossings, Layout.Buildings, Layout.Lamps, Layout.Trees);
	if (OutReport)
	{
		*OutReport = Report;
	}
	return Layout;
}

bool CityLayout::AreEqual(const FCityLayout& A, const FCityLayout& B)
{
	return ArePiecesEqual(A.Crossings, B.Crossings) && ArePiecesEqual(A.Streets, B.Streets)
		&& ArePiecesEqual(A.Blocks, B.Blocks) && ArePiecesEqual(A.Lots, B.Lots)
		&& ArePiecesEqual(A.Built, B.Built) && ArePiecesEqual(A.Buildings, B.Buildings)
		&& ArePiecesEqual(A.Lamps, B.Lamps) && ArePiecesEqual(A.Trees, B.Trees);
}
