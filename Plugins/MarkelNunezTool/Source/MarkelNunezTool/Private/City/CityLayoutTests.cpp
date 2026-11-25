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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLayoutParkingTest, "MarkelNunezTool.City.Parking", CityTestFlags)
bool FCityLayoutParkingTest::RunTest(const FString& Parameters)
{
	FCityParams Params;
	Params.ParkingOccupancy = 0.8;
	const FCityLayout Layout = CityLayout::Generate(Params, FVector::ZeroVector, 5);

	TestTrue(TEXT("Cars were parked"), Layout.Cars.Num() > 0);
	TestTrue(TEXT("Crosswalks were generated at the ends of the streets"), Layout.Crosswalks.Num() == 2 * Layout.Streets.Num());
	TestEqual(TEXT("Every building has an entrance"), Layout.Entrances.Num(), Layout.Buildings.Num());
	TestEqual(TEXT("Each car has a cabin and four wheels"), Layout.CarParts.Num(), 5 * Layout.Cars.Num());

	// Aligned with the street and next to the curb, inside the street.
	bool bAllAligned = true;
	bool bAllAtCurb = true;
	for (const FCityPiece& Car : Layout.Cars)
	{
		const FBox2D Footprint = Car.GetFootprint();
		const FCityPiece* Street = Layout.Streets.FindByPredicate([&Footprint](const FCityPiece& Candidate)
		{
			return Candidate.GetFootprint().IsInside(Footprint.GetCenter());
		});
		if (!Street)
		{
			bAllAtCurb = false;
			continue;
		}

		const FVector StreetSize = Street->GetWorldBox().GetSize();
		const bool bStreetAlongX = StreetSize.X > StreetSize.Y;
		const double CarAxisYaw = FMath::Fmod(FMath::Abs(Car.Yaw) + 1.0, 180.0) - 1.0;
		bAllAligned &= FMath::IsNearlyEqual(CarAxisYaw, bStreetAlongX ? 0.0 : 90.0, 0.5);

		const FBox2D StreetFootprint = Street->GetFootprint();
		const double ToCurb = bStreetAlongX
			? FMath::Min(Footprint.Min.Y - StreetFootprint.Min.Y, StreetFootprint.Max.Y - Footprint.Max.Y)
			: FMath::Min(Footprint.Min.X - StreetFootprint.Min.X, StreetFootprint.Max.X - Footprint.Max.X);
		bAllAtCurb &= ToCurb >= -1.0 && ToCurb < Params.ParkingLaneWidth / 2.0;
	}
	TestTrue(TEXT("Cars are aligned with their street"), bAllAligned);
	TestTrue(TEXT("Cars park inside the street, right next to the curb"), bAllAtCurb);

	// Forbidden zones.
	const auto TooClose = [&Params](const FCityPiece& Car, TConstArrayView<FCityPiece> Zones)
	{
		const FBox2D Cleared = Car.GetFootprint().ExpandBy(Params.ParkingClearance - 2.0);
		return Zones.ContainsByPredicate([&Cleared](const FCityPiece& Zone) { return Zone.GetFootprint().ExpandBy(-1.0).Intersect(Cleared); });
	};
	TestFalse(TEXT("No car is closer than ParkingClearance to a crossing"), Layout.Cars.ContainsByPredicate([&](const FCityPiece& Car) { return TooClose(Car, Layout.Crossings); }));
	TestFalse(TEXT("No car is closer than ParkingClearance to a crosswalk"), Layout.Cars.ContainsByPredicate([&](const FCityPiece& Car) { return TooClose(Car, Layout.Crosswalks); }));
	TestFalse(TEXT("No car blocks an entrance"), AnyOverlap(Layout.Cars, Layout.Entrances, false));
	TestFalse(TEXT("Cars do not overlap each other"), AnyOverlap(Layout.Cars, Layout.Cars, true));

	FCityParams Empty = Params;
	Empty.ParkingOccupancy = 0.0;
	TestEqual(TEXT("ParkingOccupancy 0 parks no cars"), CityLayout::Generate(Empty, FVector::ZeroVector, 5).Cars.Num(), 0);

	FCityParams Full = Params;
	Full.ParkingOccupancy = 1.0;
	TestTrue(TEXT("ParkingOccupancy 1 parks more cars"), CityLayout::Generate(Full, FVector::ZeroVector, 5).Cars.Num() > Layout.Cars.Num());

	FCityParams FarFromCrossings = Params;
	FarFromCrossings.ParkingClearance = 1500.0;
	TestTrue(TEXT("A larger clearance leaves fewer cars"), CityLayout::Generate(FarFromCrossings, FVector::ZeroVector, 5).Cars.Num() < Layout.Cars.Num());

	const auto CountLooks = [](const FCityLayout& InLayout, TSet<int32>& OutModels, TSet<int32>& OutColors)
	{
		for (const FCityPiece& Car : InLayout.Cars)
		{
			OutModels.Add(Car.ModelIndex);
			OutColors.Add(Car.ColorIndex);
		}
	};
	FCityParams Uniform = Params;
	Uniform.CarVariation = 0.0;
	TSet<int32> UniformModels;
	TSet<int32> UniformColors;
	CountLooks(CityLayout::Generate(Uniform, FVector::ZeroVector, 5), UniformModels, UniformColors);
	TestTrue(TEXT("CarVariation 0 makes every car the same model and color"), UniformModels.Num() == 1 && UniformColors.Num() == 1);

	FCityParams Varied = Params;
	Varied.CarVariation = 1.0;
	TSet<int32> VariedModels;
	TSet<int32> VariedColors;
	CountLooks(CityLayout::Generate(Varied, FVector::ZeroVector, 5), VariedModels, VariedColors);
	TestEqual(TEXT("CarVariation 1 uses every model"), VariedModels.Num(), CityLayout::GetCarModelCount());
	TestEqual(TEXT("CarVariation 1 uses every color"), VariedColors.Num(), CityLayout::GetCarPalette().Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCityLayoutSignsTest, "MarkelNunezTool.City.Signs", CityTestFlags)
bool FCityLayoutSignsTest::RunTest(const FString& Parameters)
{
	const FCityParams Params;
	const FCityLayout Layout = CityLayout::Generate(Params, FVector::ZeroVector, 9);

	const auto CountType = [](const FCityLayout& InLayout, const ECitySignType Type)
	{
		return InLayout.Signs.FilterByPredicate([Type](const FCityPiece& Sign) { return Sign.SignType == static_cast<int32>(Type); }).Num();
	};
	TestTrue(TEXT("Stop signs were placed"), CountType(Layout, ECitySignType::Stop) > 0);
	TestTrue(TEXT("Yield signs were placed"), CountType(Layout, ECitySignType::Yield) > 0);
	TestTrue(TEXT("Direction signs were placed"), CountType(Layout, ECitySignType::Direction) > 0);
	TestTrue(TEXT("Speed limit signs were placed"), CountType(Layout, ECitySignType::SpeedLimit) > 0);

	// On the sidewalk, never on the road, and clear of lamps and trees.
	TArray<FCityPiece> Road = Layout.Streets;
	Road.Append(Layout.Crossings);
	TArray<FCityPiece> AllSignPieces = Layout.Signs;
	AllSignPieces.Append(Layout.SignParts);
	TestFalse(TEXT("No sign piece stands on the road"), AnyOverlap(AllSignPieces, Road, false));
	const bool bAllOnSidewalk = !Layout.Signs.ContainsByPredicate([&Layout](const FCityPiece& Sign)
	{
		const FVector2D Base(Sign.Location);
		return !Layout.Blocks.ContainsByPredicate([&Base](const FCityPiece& Block) { return Block.GetFootprint().IsInside(Base); });
	});
	TestTrue(TEXT("Every sign stands on a sidewalk"), bAllOnSidewalk);
	const bool bClearOfProps = !Layout.Signs.ContainsByPredicate([&Layout, &Params](const FCityPiece& Sign)
	{
		const auto Near = [&Sign, &Params](const FCityPiece& Prop)
		{
			return FVector2D::Distance(FVector2D(Prop.Location), FVector2D(Sign.Location)) < Params.SignPropClearance - 1.0;
		};
		return Layout.Lamps.ContainsByPredicate(Near) || Layout.Trees.ContainsByPredicate(Near);
	});
	TestTrue(TEXT("Signs keep clear of lamps and trees"), bClearOfProps);

	// Intersection signs face the approaching traffic, stand on its right and follow the priority rules.
	bool bFacesTraffic = true;
	bool bOnTheRight = true;
	bool bRightPriority = true;
	bool bRespectsDistance = true;
	for (const FCityPiece& Sign : Layout.Signs)
	{
		if (Sign.SignType == static_cast<int32>(ECitySignType::SpeedLimit))
		{
			continue;
		}

		const FVector2D Position(Sign.Location);
		const FCityPiece* Crossing = nullptr;
		for (const FCityPiece& Candidate : Layout.Crossings)
		{
			if (!Crossing || FVector2D::Distance(FVector2D(Candidate.Location), Position) < FVector2D::Distance(FVector2D(Crossing->Location), Position))
			{
				Crossing = &Candidate;
			}
		}

		const FVector2D Facing(FMath::Cos(FMath::DegreesToRadians(Sign.Yaw)), FMath::Sin(FMath::DegreesToRadians(Sign.Yaw)));
		const FVector2D ToSign = Position - FVector2D(Crossing->Location);
		bFacesTraffic &= FVector2D::DotProduct(Facing, ToSign) > 0.0;

		const FVector2D Drive = -Facing;
		const FVector2D Right(Drive.Y, -Drive.X);
		bOnTheRight &= FVector2D::DotProduct(ToSign, Right) > Params.StreetWidth / 2.0;

		const double FromEdge = FMath::Abs(FVector2D::DotProduct(ToSign, Facing)) - Params.StreetWidth / 2.0;
		const double Expected = Params.SignDistance + (Sign.SignType == static_cast<int32>(ECitySignType::Direction) ? Params.DirectionSignSetback : 0.0);
		bRespectsDistance &= FromEdge >= Expected - 1.0;

		const bool bAlongX = FMath::Abs(Facing.X) > 0.5;
		const bool bOwnMajor = CityLayout::IsMajorStreet(Params, bAlongX ? Crossing->Row : Crossing->Col);
		const bool bCrossMajor = CityLayout::IsMajorStreet(Params, bAlongX ? Crossing->Col : Crossing->Row);
		if (Sign.SignType == static_cast<int32>(ECitySignType::Stop))
		{
			bRightPriority &= !bOwnMajor && bCrossMajor;
		}
		else if (Sign.SignType == static_cast<int32>(ECitySignType::Yield))
		{
			bRightPriority &= bOwnMajor == bCrossMajor && !bAlongX;
		}
		else
		{
			bRightPriority &= bOwnMajor && bCrossMajor;
		}
	}
	TestTrue(TEXT("Intersection signs face the approaching traffic"), bFacesTraffic);
	TestTrue(TEXT("Intersection signs stand on the driver's right"), bOnTheRight);
	TestTrue(TEXT("Stop on minor streets at main streets, yield between equal streets, direction where main streets cross"), bRightPriority);
	TestTrue(TEXT("Intersection signs are at least SignDistance from the intersection"), bRespectsDistance);

	// Parameters.
	FCityParams OnlySpeed = Params;
	OnlySpeed.bStopSigns = false;
	OnlySpeed.bYieldSigns = false;
	OnlySpeed.bDirectionSigns = false;
	const FCityLayout OnlySpeedLayout = CityLayout::Generate(OnlySpeed, FVector::ZeroVector, 9);
	TestEqual(TEXT("Disabled types are not placed"), OnlySpeedLayout.Signs.Num(), CountType(OnlySpeedLayout, ECitySignType::SpeedLimit));

	FCityParams NoSpeed = Params;
	NoSpeed.SignSpacing = 0.0;
	TestEqual(TEXT("SignSpacing 0 places no speed limits"), CountType(CityLayout::Generate(NoSpeed, FVector::ZeroVector, 9), ECitySignType::SpeedLimit), 0);

	FCityParams DenseSpeed = Params;
	DenseSpeed.SignSpacing = 1500.0;
	TestTrue(TEXT("A shorter SignSpacing places more speed limits"), CountType(CityLayout::Generate(DenseSpeed, FVector::ZeroVector, 9), ECitySignType::SpeedLimit) > CountType(Layout, ECitySignType::SpeedLimit));

	FCityParams StopOnly = Params;
	StopOnly.bYieldSigns = false;
	TestEqual(TEXT("With yield disabled, its intersections get stop signs instead"),
		CountType(CityLayout::Generate(StopOnly, FVector::ZeroVector, 9), ECitySignType::Stop),
		CountType(Layout, ECitySignType::Stop) + CountType(Layout, ECitySignType::Yield));
	return true;
}

#endif
