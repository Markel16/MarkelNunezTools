// Copyright Epic Games, Inc. All Rights Reserved.

#include "MarkelNunezToolEditorModule.h"

#include "CityToolLibrary.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

const FName FMarkelNunezToolEditorModule::CityTabName(TEXT("MarkelNunezCityTab"));

namespace CityToolTab
{
	template <typename T>
	TSharedRef<SWidget> MakeRow(const FText& Label, T& Value, const T MinValue, const T MaxValue)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.45f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.55f)
			[
				SNew(SSpinBox<T>)
				.MinValue(MinValue)
				.MaxValue(MaxValue)
				.Value_Lambda([&Value]() { return Value; })
				.OnValueChanged_Lambda([&Value](const T NewValue) { Value = NewValue; })
			];
	}

	TSharedRef<SWidget> MakeButton(const FText& Label, TFunction<void()> OnClicked)
	{
		return SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(Label)
			.OnClicked_Lambda([OnClicked = MoveTemp(OnClicked)]()
			{
				OnClicked();
				return FReply::Handled();
			});
	}
}

TSharedRef<SDockTab> FMarkelNunezToolEditorModule::OnSpawnCityTab(const FSpawnTabArgs& Args)
{
	using namespace CityToolTab;

	const auto Generate = [this]()
	{
		const bool bGenerated = UCityToolLibrary::GenerateCity(CitySizeMeters, BlockSizeMeters, BuildingDensity, BuildingHeightMeters, BuildingVariation, CitySeed);
		CityStatus = bGenerated
			? FText::Format(NSLOCTEXT("MarkelNunezTool", "CityGenerated", "Generated with seed {0}."), FText::AsNumber(CitySeed))
			: NSLOCTEXT("MarkelNunezTool", "CityGenerateFailed", "Generation failed, see the Output Log (LogTemp / LogPCG).");
	};

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeRow(NSLOCTEXT("MarkelNunezTool", "CitySizeLabel", "City size (m)"), CitySizeMeters, 50.0, 3000.0)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeRow(NSLOCTEXT("MarkelNunezTool", "BlockSizeLabel", "Block size (m)"), BlockSizeMeters, 15.0, 300.0)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeRow(NSLOCTEXT("MarkelNunezTool", "BuildingDensityLabel", "Building density (0-1)"), BuildingDensity, 0.0, 1.0)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeRow(NSLOCTEXT("MarkelNunezTool", "BuildingHeightLabel", "Building height (m)"), BuildingHeightMeters, 3.0, 300.0)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeRow(NSLOCTEXT("MarkelNunezTool", "BuildingVariationLabel", "Building variation (0-1)"), BuildingVariation, 0.0, 1.0)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeRow(NSLOCTEXT("MarkelNunezTool", "CitySeedLabel", "Seed"), CitySeed, 0, 999999)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 12.0f, 4.0f, 4.0f)
			[
				MakeButton(NSLOCTEXT("MarkelNunezTool", "GenerateCityButton", "Generate"), Generate)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeButton(NSLOCTEXT("MarkelNunezTool", "CleanupCityButton", "Clean up"), [this]()
				{
					UCityToolLibrary::CleanupCity();
					CityStatus = NSLOCTEXT("MarkelNunezTool", "CityCleaned", "City removed. Check the Outliner: nothing should be left.");
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeButton(NSLOCTEXT("MarkelNunezTool", "TestDeterminismButton", "Test determinism"), [this]()
				{
					FString Report;
					const bool bPassed = UCityToolLibrary::TestDeterminism(Report, CitySizeMeters, BlockSizeMeters, BuildingDensity, BuildingHeightMeters, BuildingVariation);
					CityStatus = FText::FromString(FString::Printf(TEXT("%s %s"), bPassed ? TEXT("PASSED.") : TEXT("FAILED."), *Report));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f)
			[
				MakeButton(NSLOCTEXT("MarkelNunezTool", "RebuildGraphsButton", "Rebuild PCG graphs"), [this]()
				{
					CityStatus = UCityToolLibrary::BuildCityGraphs()
						? NSLOCTEXT("MarkelNunezTool", "GraphsBuilt", "PCG_City and its subgraphs were rebuilt in /MarkelNunezTool/City.")
						: NSLOCTEXT("MarkelNunezTool", "GraphsFailed", "Some graph connections failed, see the Output Log.");
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 12.0f, 4.0f, 4.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]() { return CityStatus; })
			]
		];
}
