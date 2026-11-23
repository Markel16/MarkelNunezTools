// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/CityLayout.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CityTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool AnyOverlap(TConstArrayView<FCityPiece> A, TConstArrayView<FCityPiece> B, const bool bSameArray)
	{
		for (int32 I = 0; I < A.Num(); ++I)
		{
			for (int32 J = bSameArray ? I + 1 : 0; J < B.Num(); ++J)
			{
				const FBox2D FootprintA = A[I].GetFootprint().ExpandBy(-1.0);
				const FBox2D FootprintB = B[J].GetFootprint().ExpandBy(-1.0);
				if (FootprintA.Intersect(FootprintB))
				{
					return true;
				}
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLayoutGridTest, "MarkelNunezTool.City.Grid", CityTestFlags)
bool FCityLayoutGridTest::RunTest(const FString& Parameters)
{
	const FCityParams Params;
	const FCityLayout Layout = CityLayout::Generate(Params, FVector::ZeroVector, 1234);
	const int32 Count = Params.CrossingsPerSide();

	TestEqual(TEXT("Crossings per side for a 400 m city with 60 m blocks"), Count, 6);
	TestEqual(TEXT("Crossings"), Layout.Crossings.Num(), Count * Count);
	TestEqual(TEXT("Streets"), Layout.Streets.Num(), 2 * Count * (Count - 1));
	TestEqual(TEXT("Blocks"), Layout.Blocks.Num(), (Count - 1) * (Count - 1));
	TestTrue(TEXT("Lots were generated"), Layout.Lots.Num() > Layout.Blocks.Num());
	TestTrue(TEXT("Buildings were generated"), Layout.Buildings.Num() > 0);
	TestTrue(TEXT("Lamps were generated"), Layout.Lamps.Num() > 0);
	TestTrue(TEXT("Trees were generated"), Layout.Trees.Num() > 0);
	TestTrue(TEXT("Trees survive the overlap cleanup as often as lamps"), Layout.Trees.Num() >= Layout.Lamps.Num() / 2);
	TestTrue(TEXT("The grid is centered on the origin"), Layout.Crossings[0].Location.X < 0.0 && Layout.Crossings.Last().Location.X > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLayoutOverlapTest, "MarkelNunezTool.City.NoOverlaps", CityTestFlags)
bool FCityLayoutOverlapTest::RunTest(const FString& Parameters)
{
	FCityParams Params;
	Params.BuildingDensity = 1.0;
	Params.BuildingVariation = 1.0;

	FCityOverlapReport Report;
	const FCityLayout Layout = CityLayout::Generate(Params, FVector(1000.0, -500.0, 0.0), 99, &Report);

	TArray<FCityPiece> Road = Layout.Streets;
	Road.Append(Layout.Crossings);

	TestFalse(TEXT("Streets and crossings do not overlap"), AnyOverlap(Layout.Streets, Layout.Crossings, false));
	TestFalse(TEXT("Blocks do not overlap the road"), AnyOverlap(Layout.Blocks, Road, false));
	TestFalse(TEXT("Buildings do not overlap the road"), AnyOverlap(Layout.Buildings, Road, false));
	TestFalse(TEXT("Buildings do not overlap each other"), AnyOverlap(Layout.Buildings, Layout.Buildings, true));
	TestFalse(TEXT("Lamps do not overlap the road or buildings"), AnyOverlap(Layout.Lamps, Road, false) || AnyOverlap(Layout.Lamps, Layout.Buildings, false));
	TestFalse(TEXT("Trees do not overlap the road or buildings"), AnyOverlap(Layout.Trees, Road, false) || AnyOverlap(Layout.Trees, Layout.Buildings, false));
	TestEqual(TEXT("Every layer passes its checks"), Report.FailedChecks.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLayoutDeterminismTest, "MarkelNunezTool.City.Determinism", CityTestFlags)
bool FCityLayoutDeterminismTest::RunTest(const FString& Parameters)
{
	const FCityParams Params;
	const FCityLayout First = CityLayout::Generate(Params, FVector::ZeroVector, 7);
	const FCityLayout Second = CityLayout::Generate(Params, FVector::ZeroVector, 7);
	const FCityLayout Other = CityLayout::Generate(Params, FVector::ZeroVector, 8);

	TestTrue(TEXT("Same seed, same city"), CityLayout::AreEqual(First, Second));
	TestFalse(TEXT("New seed, new city"), CityLayout::AreEqual(First, Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLayoutParametersTest, "MarkelNunezTool.City.Parameters", CityTestFlags)
bool FCityLayoutParametersTest::RunTest(const FString& Parameters)
{
	const FCityParams Base;
	const FCityLayout BaseLayout = CityLayout::Generate(Base, FVector::ZeroVector, 1);

	FCityParams Bigger = Base;
	Bigger.CitySize *= 2.0;
	TestTrue(TEXT("CitySize adds crossings"), CityLayout::Generate(Bigger, FVector::ZeroVector, 1).Crossings.Num() > BaseLayout.Crossings.Num());

	FCityParams SmallBlocks = Base;
	SmallBlocks.BlockSize /= 2.0;
	TestTrue(TEXT("BlockSize changes the number of blocks"), CityLayout::Generate(SmallBlocks, FVector::ZeroVector, 1).Blocks.Num() > BaseLayout.Blocks.Num());

	FCityParams Empty = Base;
	Empty.BuildingDensity = 0.0;
	TestEqual(TEXT("BuildingDensity 0 leaves an open area"), CityLayout::Generate(Empty, FVector::ZeroVector, 1).Buildings.Num(), 0);

	FCityParams Full = Base;
	Full.BuildingDensity = 1.0;
	const FCityLayout FullLayout = CityLayout::Generate(Full, FVector::ZeroVector, 1);
	const int32 FrontageLots = FullLayout.Lots.FilterByPredicate([](const FCityPiece& Lot) { return Lot.bFrontage; }).Num();
	TestEqual(TEXT("BuildingDensity 1 builds every frontage lot"), FullLayout.Buildings.Num(), FrontageLots);

	FCityParams Tall = Base;
	Tall.BuildingHeight *= 2.0;
	const auto AverageHeight = [](const FCityLayout& Layout)
	{
		double Sum = 0.0;
		for (const FCityPiece& Building : Layout.Buildings)
		{
			Sum += Building.Height;
		}
		return Layout.Buildings.IsEmpty() ? 0.0 : Sum / Layout.Buildings.Num();
	};
	TestTrue(TEXT("BuildingHeight raises the buildings"), AverageHeight(CityLayout::Generate(Tall, FVector::ZeroVector, 1)) > AverageHeight(BaseLayout));

	FCityParams Uniform = Base;
	Uniform.BuildingVariation = 0.0;
	const FCityLayout UniformLayout = CityLayout::Generate(Uniform, FVector::ZeroVector, 1);
	const bool bAllSameHeight = UniformLayout.Buildings.ContainsByPredicate([&Uniform](const FCityPiece& Building)
	{
		return !FMath::IsNearlyEqual(Building.Height, Uniform.BuildingHeight);
	}) == false;
	TestTrue(TEXT("BuildingVariation 0 makes every building the same height"), bAllSameHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLayoutParksTest, "MarkelNunezTool.City.Parks", CityTestFlags)
bool FCityLayoutParksTest::RunTest(const FString& Parameters)
{
	FCityParams Params;
	Params.BuildingDensity = 1.0;
	Params.ParkRatio = 0.2;
	const FCityLayout Layout = CityLayout::Generate(Params, FVector::ZeroVector, 3);

	TestEqual(TEXT("ParkRatio picks exactly that share of blocks"), Layout.Parks.Num(), FMath::RoundToInt32(0.2 * Layout.Blocks.Num()));
	TestEqual(TEXT("Every block is either a park or buildable"), Layout.Parks.Num() + Layout.BuildableBlocks.Num(), Layout.Blocks.Num());
	TestEqual(TEXT("Each park gets a lawn"), Layout.Lawns.Num(), Layout.Parks.Num());
	TestTrue(TEXT("Parks have trees"), Layout.ParkTrees.Num() > 0);
	TestTrue(TEXT("Parks have bushes"), Layout.ParkBushes.Num() > 0);

	TSet<int32> ParkIDs;
	for (const FCityPiece& Park : Layout.Parks)
	{
		ParkIDs.Add(Park.BlockID);
	}
	TestFalse(TEXT("No building stands in a park"), Layout.Buildings.ContainsByPredicate([&ParkIDs](const FCityPiece& Building) { return ParkIDs.Contains(Building.BlockID); }));

	TArray<FCityPiece> Plants = Layout.ParkTrees;
	Plants.Append(Layout.ParkBushes);
	TestFalse(TEXT("Park plants do not overlap each other"), AnyOverlap(Plants, Plants, true));

	const bool bAllInsideLawns = !Plants.ContainsByPredicate([&Layout](const FCityPiece& Plant)
	{
		const FVector2D Base(Plant.Location);
		return !Layout.Lawns.ContainsByPredicate([&Base](const FCityPiece& Lawn) { return Lawn.GetFootprint().IsInside(Base); });
	});
	TestTrue(TEXT("Every park plant grows on a lawn, never on the sidewalk"), bAllInsideLawns);

	FCityParams NoParks = Params;
	NoParks.ParkRatio = 0.0;
	TestEqual(TEXT("ParkRatio 0 makes no parks"), CityLayout::Generate(NoParks, FVector::ZeroVector, 3).Parks.Num(), 0);

	FCityParams NoTrees = Params;
	NoTrees.ParkTreeDensity = 0.0;
	TestEqual(TEXT("ParkTreeDensity 0 leaves parks without trees"), CityLayout::Generate(NoTrees, FVector::ZeroVector, 3).ParkTrees.Num(), 0);

	FCityParams DenseTrees = Params;
	DenseTrees.ParkTreeDensity = 1.0;
	const FCityLayout DenseLayout = CityLayout::Generate(DenseTrees, FVector::ZeroVector, 3);
	TestTrue(TEXT("ParkTreeDensity 1 fills every cell with a tree"), DenseLayout.ParkTrees.Num() > Layout.ParkTrees.Num() && DenseLayout.ParkBushes.IsEmpty());

	const auto ScaleRange = [](const FCityLayout& InLayout)
	{
		double Min = TNumericLimits<double>::Max();
		double Max = 0.0;
		for (const FCityPiece& Tree : InLayout.ParkTrees)
		{
			Min = FMath::Min(Min, Tree.Scale.X);
			Max = FMath::Max(Max, Tree.Scale.X);
		}
		return Max - Min;
	};
	FCityParams Uniform = Params;
	Uniform.VegetationScaleVariation = 0.0;
	TestTrue(TEXT("VegetationScaleVariation 0 makes every tree the same size"), ScaleRange(CityLayout::Generate(Uniform, FVector::ZeroVector, 3)) < UE_KINDA_SMALL_NUMBER);
	TestTrue(TEXT("VegetationScaleVariation above 0 varies the tree size"), ScaleRange(Layout) > 0.05);
	return true;
}

#endif
