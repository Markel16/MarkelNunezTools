// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/PCGCityNodes.h"

#include "City/CityLayout.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGCity"

namespace PCGCityPins
{
	const FName Params(TEXT("Params"));
	const FName Crossings(TEXT("Crossings"));
	const FName Streets(TEXT("Streets"));
	const FName Blocks(TEXT("Blocks"));
	const FName Lots(TEXT("Lots"));
	const FName Built(TEXT("Built"));
	const FName Buildings(TEXT("Buildings"));
	const FName Lamps(TEXT("Lamps"));
	const FName Trees(TEXT("Trees"));
	const FName Buildable(TEXT("Buildable"));
	const FName Parks(TEXT("Parks"));
	const FName Lawns(TEXT("Lawns"));
	const FName ParkTrees(TEXT("ParkTrees"));
	const FName ParkBushes(TEXT("ParkBushes"));
}

namespace
{
	namespace CityAttributes
	{
		const FName Col(TEXT("Col"));
		const FName Row(TEXT("Row"));
		const FName BlockID(TEXT("BlockID"));
		const FName Frontage(TEXT("Frontage"));
		const FName BlockCenter(TEXT("BlockCenter"));
		const FName Height(TEXT("Height"));
	}

	enum class ECityAttributes : uint8
	{
		None = 0,
		ColRow = 1 << 0,
		BlockID = 1 << 1,
		Frontage = 1 << 2,
		BlockCenter = 1 << 3,
		Height = 1 << 4,
	};
	ENUM_CLASS_FLAGS(ECityAttributes);

	// The Attribute Set on the Params pin uses the same names as the FCityParams fields.
	const TPair<FName, double FCityParams::*> ParamFields[] = {
		{ TEXT("CitySize"), &FCityParams::CitySize },
		{ TEXT("BlockSize"), &FCityParams::BlockSize },
		{ TEXT("BuildingDensity"), &FCityParams::BuildingDensity },
		{ TEXT("BuildingHeight"), &FCityParams::BuildingHeight },
		{ TEXT("BuildingVariation"), &FCityParams::BuildingVariation },
		{ TEXT("ParkRatio"), &FCityParams::ParkRatio },
		{ TEXT("ParkTreeDensity"), &FCityParams::ParkTreeDensity },
		{ TEXT("VegetationScaleVariation"), &FCityParams::VegetationScaleVariation },
		{ TEXT("StreetWidth"), &FCityParams::StreetWidth },
		{ TEXT("SidewalkWidth"), &FCityParams::SidewalkWidth },
		{ TEXT("LotSize"), &FCityParams::LotSize },
		{ TEXT("PropSpacing"), &FCityParams::PropSpacing },
		{ TEXT("TreeScale"), &FCityParams::TreeScale },
	};

	bool ReadParams(FPCGContext* Context, FCityParams& OutParams)
	{
		for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGCityPins::Params))
		{
			const UPCGParamData* ParamData = Cast<UPCGParamData>(Input.Data.Get());
			const UPCGMetadata* Metadata = ParamData ? ParamData->ConstMetadata() : nullptr;
			if (!Metadata)
			{
				continue;
			}

			for (const TPair<FName, double FCityParams::*>& Field : ParamFields)
			{
				if (const FPCGMetadataAttribute<double>* Attribute = Metadata->GetConstTypedAttribute<double>(Field.Key))
				{
					OutParams.*Field.Value = Attribute->GetValueFromItemKey(PCGFirstEntryKey);
				}
			}
			return true;
		}
		return false;
	}

	TArray<FCityPiece> ReadPieces(FPCGContext* Context, const FName Pin)
	{
		TArray<FCityPiece> Pieces;
		for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(Pin))
		{
			const UPCGPointData* PointData = Cast<UPCGPointData>(Input.Data.Get());
			if (!PointData)
			{
				continue;
			}

			const UPCGMetadata* Metadata = PointData->ConstMetadata();
			const FPCGMetadataAttribute<int32>* ColAttribute = Metadata->GetConstTypedAttribute<int32>(CityAttributes::Col);
			const FPCGMetadataAttribute<int32>* RowAttribute = Metadata->GetConstTypedAttribute<int32>(CityAttributes::Row);
			const FPCGMetadataAttribute<int32>* BlockIDAttribute = Metadata->GetConstTypedAttribute<int32>(CityAttributes::BlockID);
			const FPCGMetadataAttribute<bool>* FrontageAttribute = Metadata->GetConstTypedAttribute<bool>(CityAttributes::Frontage);
			const FPCGMetadataAttribute<FVector>* BlockCenterAttribute = Metadata->GetConstTypedAttribute<FVector>(CityAttributes::BlockCenter);
			const FPCGMetadataAttribute<double>* HeightAttribute = Metadata->GetConstTypedAttribute<double>(CityAttributes::Height);

			for (const FPCGPoint& Point : PointData->GetPoints())
			{
				FCityPiece& Piece = Pieces.AddDefaulted_GetRef();
				Piece.Location = Point.Transform.GetLocation();
				Piece.Yaw = Point.Transform.Rotator().Yaw;
				Piece.Scale = Point.Transform.GetScale3D();
				Piece.LocalBounds = FBox(Point.BoundsMin, Point.BoundsMax);
				Piece.Seed = Point.Seed;
				Piece.Density = Point.Density;

				const PCGMetadataEntryKey Key = Point.MetadataEntry;
				Piece.Col = ColAttribute ? ColAttribute->GetValueFromItemKey(Key) : INDEX_NONE;
				Piece.Row = RowAttribute ? RowAttribute->GetValueFromItemKey(Key) : INDEX_NONE;
				Piece.BlockID = BlockIDAttribute ? BlockIDAttribute->GetValueFromItemKey(Key) : INDEX_NONE;
				Piece.bFrontage = FrontageAttribute && FrontageAttribute->GetValueFromItemKey(Key);
				Piece.BlockCenter = BlockCenterAttribute ? BlockCenterAttribute->GetValueFromItemKey(Key) : FVector::ZeroVector;
				Piece.Height = HeightAttribute ? HeightAttribute->GetValueFromItemKey(Key) : 0.0;
			}
		}
		return Pieces;
	}

	void WritePieces(FPCGContext* Context, const FName Pin, TConstArrayView<FCityPiece> Pieces, const ECityAttributes Attributes)
	{
		UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		UPCGMetadata* Metadata = PointData->MutableMetadata();

		const auto CreateIf = [Metadata, Attributes]<typename T>(const ECityAttributes Flag, const FName Name, const T& Default) -> FPCGMetadataAttribute<T>*
		{
			return EnumHasAnyFlags(Attributes, Flag) ? Metadata->CreateAttribute<T>(Name, Default, false, false) : nullptr;
		};
		FPCGMetadataAttribute<int32>* ColAttribute = CreateIf(ECityAttributes::ColRow, CityAttributes::Col, int32(INDEX_NONE));
		FPCGMetadataAttribute<int32>* RowAttribute = CreateIf(ECityAttributes::ColRow, CityAttributes::Row, int32(INDEX_NONE));
		FPCGMetadataAttribute<int32>* BlockIDAttribute = CreateIf(ECityAttributes::BlockID, CityAttributes::BlockID, int32(INDEX_NONE));
		FPCGMetadataAttribute<bool>* FrontageAttribute = CreateIf(ECityAttributes::Frontage, CityAttributes::Frontage, false);
		FPCGMetadataAttribute<FVector>* BlockCenterAttribute = CreateIf(ECityAttributes::BlockCenter, CityAttributes::BlockCenter, FVector::ZeroVector);
		FPCGMetadataAttribute<double>* HeightAttribute = CreateIf(ECityAttributes::Height, CityAttributes::Height, 0.0);

		TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
		Points.Reserve(Pieces.Num());
		for (const FCityPiece& Piece : Pieces)
		{
			FPCGPoint& Point = Points.Emplace_GetRef(Piece.GetTransform(), Piece.Density, Piece.Seed);
			Point.BoundsMin = Piece.LocalBounds.Min;
			Point.BoundsMax = Piece.LocalBounds.Max;
			Point.Steepness = 1.0f;

			if (Attributes == ECityAttributes::None)
			{
				continue;
			}

			Metadata->InitializeOnSet(Point.MetadataEntry);
			if (ColAttribute)
			{
				ColAttribute->SetValue(Point.MetadataEntry, Piece.Col);
				RowAttribute->SetValue(Point.MetadataEntry, Piece.Row);
			}
			if (BlockIDAttribute)
			{
				BlockIDAttribute->SetValue(Point.MetadataEntry, Piece.BlockID);
			}
			if (FrontageAttribute)
			{
				FrontageAttribute->SetValue(Point.MetadataEntry, Piece.bFrontage);
			}
			if (BlockCenterAttribute)
			{
				BlockCenterAttribute->SetValue(Point.MetadataEntry, Piece.BlockCenter);
			}
			if (HeightAttribute)
			{
				HeightAttribute->SetValue(Point.MetadataEntry, Piece.Height);
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = PointData;
		Output.Pin = Pin;
	}

	FPCGPinProperties MakePin(const FName Label)
	{
		const bool bIsParams = Label == PCGCityPins::Params;
		FPCGPinProperties Pin(Label, FPCGDataTypeIdentifier{ bIsParams ? EPCGDataType::Param : EPCGDataType::Point }, /*bAllowMultipleConnections=*/!bIsParams);
		if (bIsParams)
		{
			Pin.SetRequiredPin();
		}
		return Pin;
	}

	class FPCGCityParamsElement : public IPCGElement
	{
	protected:
		virtual bool ExecuteInternal(FPCGContext* Context) const override
		{
			const UPCGCityParamsSettings* Settings = Context->GetInputSettings<UPCGCityParamsSettings>();
			check(Settings);

			const FCityParams Params = Settings->ToCityParams();
			FString Error;
			if (!Params.IsValid(&Error))
			{
				PCGE_LOG(Error, GraphAndLog, FText::FromString(Error));
				return true;
			}

			UPCGParamData* ParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
			UPCGMetadata* Metadata = ParamData->MutableMetadata();
			const PCGMetadataEntryKey Key = Metadata->AddEntry();
			for (const TPair<FName, double FCityParams::*>& Field : ParamFields)
			{
				if (FPCGMetadataAttribute<double>* Attribute = Metadata->CreateAttribute<double>(Field.Key, Params.*Field.Value, false, false))
				{
					Attribute->SetValue(Key, Params.*Field.Value);
				}
			}

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
			Output.Data = ParamData;
			Output.Pin = PCGCityPins::Params;
			return true;
		}
	};

	class FPCGCityLayerElement : public IPCGElement
	{
	public:
		virtual bool IsCacheable(const UPCGSettings* InSettings) const override
		{
			const UPCGCityLayerSettings* Settings = Cast<UPCGCityLayerSettings>(InSettings);
			return (!Settings || !Settings->DependsOnActorTransform()) && IPCGElement::IsCacheable(InSettings);
		}

	protected:
		virtual bool ExecuteInternal(FPCGContext* Context) const override
		{
			const UPCGCityLayerSettings* Settings = Context->GetInputSettings<UPCGCityLayerSettings>();
			check(Settings);

			FCityParams Params;
			if (!ReadParams(Context, Params))
			{
				PCGE_LOG(Warning, GraphAndLog, LOCTEXT("MissingParams", "Nothing is connected to the Params pin, so the default city values are used."));
			}

			FString Error;
			if (!Params.IsValid(&Error))
			{
				PCGE_LOG(Error, GraphAndLog, FText::FromString(Error));
				return true;
			}

			Settings->RunLayer(Context, Params);
			return true;
		}
	};

	FVector GetOrigin(const FPCGContext* Context)
	{
		return Context->ExecutionSource.IsValid()
			? Context->ExecutionSource->GetExecutionState().GetTransform().GetLocation()
			: FVector::ZeroVector;
	}
}

FCityParams UPCGCityParamsSettings::ToCityParams() const
{
	FCityParams Params;
	Params.CitySize = CitySize;
	Params.BlockSize = BlockSize;
	Params.BuildingDensity = BuildingDensity;
	Params.BuildingHeight = BuildingHeight;
	Params.BuildingVariation = BuildingVariation;
	Params.ParkRatio = ParkRatio;
	Params.ParkTreeDensity = ParkTreeDensity;
	Params.VegetationScaleVariation = VegetationScaleVariation;
	Params.StreetWidth = StreetWidth;
	Params.SidewalkWidth = SidewalkWidth;
	Params.LotSize = LotSize;
	Params.PropSpacing = PropSpacing;
	Params.TreeScale = TreeScale;
	return Params;
}

TArray<FPCGPinProperties> UPCGCityParamsSettings::OutputPinProperties() const
{
	return { FPCGPinProperties(PCGCityPins::Params, FPCGDataTypeIdentifier{ EPCGDataType::Param }) };
}

FPCGElementPtr UPCGCityParamsSettings::CreateElement() const
{
	return MakeShared<FPCGCityParamsElement>();
}

TArray<FPCGPinProperties> UPCGCityLayerSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	for (const FName Label : GetInputLabels())
	{
		Pins.Add(MakePin(Label));
	}
	return Pins;
}

TArray<FPCGPinProperties> UPCGCityLayerSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	for (const FName Label : GetOutputLabels())
	{
		Pins.Add(MakePin(Label));
	}
	return Pins;
}

FPCGElementPtr UPCGCityLayerSettings::CreateElement() const
{
	return MakeShared<FPCGCityLayerElement>();
}

TArray<FName> UPCGCityGridSettings::GetInputLabels() const { return { PCGCityPins::Params }; }
TArray<FName> UPCGCityGridSettings::GetOutputLabels() const { return { PCGCityPins::Crossings }; }

bool UPCGCityGridSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Crossings = CityLayout::BuildCrossings(Params, GetOrigin(Context));
	WritePieces(Context, PCGCityPins::Crossings, Crossings, ECityAttributes::ColRow);
	return true;
}

TArray<FName> UPCGCityStreetsSettings::GetInputLabels() const { return { PCGCityPins::Crossings, PCGCityPins::Params }; }
TArray<FName> UPCGCityStreetsSettings::GetOutputLabels() const { return { PCGCityPins::Streets }; }

bool UPCGCityStreetsSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Crossings = ReadPieces(Context, PCGCityPins::Crossings);
	WritePieces(Context, PCGCityPins::Streets, CityLayout::BuildStreets(Params, Crossings), ECityAttributes::ColRow);
	return true;
}

TArray<FName> UPCGCityBlocksSettings::GetInputLabels() const { return { PCGCityPins::Crossings, PCGCityPins::Params }; }
TArray<FName> UPCGCityBlocksSettings::GetOutputLabels() const { return { PCGCityPins::Blocks }; }

bool UPCGCityBlocksSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Crossings = ReadPieces(Context, PCGCityPins::Crossings);
	WritePieces(Context, PCGCityPins::Blocks, CityLayout::BuildBlocks(Params, Crossings), ECityAttributes::ColRow | ECityAttributes::BlockID | ECityAttributes::BlockCenter);
	return true;
}

TArray<FName> UPCGCityParksSettings::GetInputLabels() const { return { PCGCityPins::Blocks, PCGCityPins::Params }; }
TArray<FName> UPCGCityParksSettings::GetOutputLabels() const { return { PCGCityPins::Buildable, PCGCityPins::Parks }; }

bool UPCGCityParksSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Blocks = ReadPieces(Context, PCGCityPins::Blocks);
	TArray<FCityPiece> Buildable;
	TArray<FCityPiece> Parks;
	CityLayout::SplitParks(Params, Blocks, Context->GetSeed(), Buildable, Parks);

	const ECityAttributes BlockAttributes = ECityAttributes::ColRow | ECityAttributes::BlockID | ECityAttributes::BlockCenter;
	WritePieces(Context, PCGCityPins::Buildable, Buildable, BlockAttributes);
	WritePieces(Context, PCGCityPins::Parks, Parks, BlockAttributes);
	return true;
}

TArray<FName> UPCGCityParkVegetationSettings::GetInputLabels() const { return { PCGCityPins::Parks, PCGCityPins::Params }; }
TArray<FName> UPCGCityParkVegetationSettings::GetOutputLabels() const { return { PCGCityPins::Lawns, PCGCityPins::ParkTrees, PCGCityPins::ParkBushes }; }

bool UPCGCityParkVegetationSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Parks = ReadPieces(Context, PCGCityPins::Parks);
	TArray<FCityPiece> Lawns;
	TArray<FCityPiece> Trees;
	TArray<FCityPiece> Bushes;
	CityLayout::BuildParkVegetation(Params, Parks, Context->GetSeed(), Lawns, Trees, Bushes);

	WritePieces(Context, PCGCityPins::Lawns, Lawns, ECityAttributes::BlockID);
	WritePieces(Context, PCGCityPins::ParkTrees, Trees, ECityAttributes::BlockID);
	WritePieces(Context, PCGCityPins::ParkBushes, Bushes, ECityAttributes::BlockID);
	return true;
}

TArray<FName> UPCGCityLotsSettings::GetInputLabels() const { return { PCGCityPins::Blocks, PCGCityPins::Params }; }
TArray<FName> UPCGCityLotsSettings::GetOutputLabels() const { return { PCGCityPins::Lots }; }

bool UPCGCityLotsSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Blocks = ReadPieces(Context, PCGCityPins::Blocks);
	WritePieces(Context, PCGCityPins::Lots, CityLayout::BuildLots(Params, Blocks), ECityAttributes::BlockID | ECityAttributes::Frontage | ECityAttributes::BlockCenter);
	return true;
}

TArray<FName> UPCGCityDensitySettings::GetInputLabels() const { return { PCGCityPins::Lots, PCGCityPins::Params }; }
TArray<FName> UPCGCityDensitySettings::GetOutputLabels() const { return { PCGCityPins::Built }; }

bool UPCGCityDensitySettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Lots = ReadPieces(Context, PCGCityPins::Lots);
	WritePieces(Context, PCGCityPins::Built, CityLayout::PickBuiltLots(Params, Lots, Context->GetSeed()), ECityAttributes::BlockID | ECityAttributes::Frontage | ECityAttributes::BlockCenter);
	return true;
}

TArray<FName> UPCGCityBuildingsSettings::GetInputLabels() const { return { PCGCityPins::Built, PCGCityPins::Params }; }
TArray<FName> UPCGCityBuildingsSettings::GetOutputLabels() const { return { PCGCityPins::Buildings }; }

bool UPCGCityBuildingsSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Built = ReadPieces(Context, PCGCityPins::Built);
	WritePieces(Context, PCGCityPins::Buildings, CityLayout::BuildBuildings(Params, Built, Context->GetSeed()), ECityAttributes::BlockID | ECityAttributes::Height);
	return true;
}

TArray<FName> UPCGCityPropsSettings::GetInputLabels() const { return { PCGCityPins::Blocks, PCGCityPins::Crossings, PCGCityPins::Params }; }
TArray<FName> UPCGCityPropsSettings::GetOutputLabels() const { return { PCGCityPins::Lamps, PCGCityPins::Trees }; }

bool UPCGCityPropsSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Blocks = ReadPieces(Context, PCGCityPins::Blocks);
	const TArray<FCityPiece> Crossings = ReadPieces(Context, PCGCityPins::Crossings);

	TArray<FCityPiece> Lamps;
	TArray<FCityPiece> Trees;
	CityLayout::BuildProps(Params, Blocks, Crossings, Context->GetSeed(), Lamps, Trees);
	WritePieces(Context, PCGCityPins::Lamps, Lamps, ECityAttributes::BlockID);
	WritePieces(Context, PCGCityPins::Trees, Trees, ECityAttributes::BlockID);
	return true;
}

TArray<FName> UPCGCityOverlapsSettings::GetInputLabels() const
{
	return { PCGCityPins::Streets, PCGCityPins::Crossings, PCGCityPins::Buildings, PCGCityPins::Lamps, PCGCityPins::Trees, PCGCityPins::Params };
}

TArray<FName> UPCGCityOverlapsSettings::GetOutputLabels() const
{
	return { PCGCityPins::Buildings, PCGCityPins::Lamps, PCGCityPins::Trees };
}

bool UPCGCityOverlapsSettings::RunLayer(FPCGContext* Context, const FCityParams& Params) const
{
	const TArray<FCityPiece> Streets = ReadPieces(Context, PCGCityPins::Streets);
	const TArray<FCityPiece> Crossings = ReadPieces(Context, PCGCityPins::Crossings);
	TArray<FCityPiece> Buildings = ReadPieces(Context, PCGCityPins::Buildings);
	TArray<FCityPiece> Lamps = ReadPieces(Context, PCGCityPins::Lamps);
	TArray<FCityPiece> Trees = ReadPieces(Context, PCGCityPins::Trees);

	const FCityOverlapReport Report = CityLayout::CleanOverlaps(Params, Streets, Crossings, Buildings, Lamps, Trees);
	for (const FString& FailedCheck : Report.FailedChecks)
	{
		PCGLog::LogWarningOnGraph(FText::FromString(FailedCheck), Context);
	}

	WritePieces(Context, PCGCityPins::Buildings, Buildings, ECityAttributes::BlockID | ECityAttributes::Height);
	WritePieces(Context, PCGCityPins::Lamps, Lamps, ECityAttributes::BlockID);
	WritePieces(Context, PCGCityPins::Trees, Trees, ECityAttributes::BlockID);
	return true;
}

#undef LOCTEXT_NAMESPACE
