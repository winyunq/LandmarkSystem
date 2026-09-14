// Copyright 2026 Winyunq. All Rights Reserved.

#include "MassUnitInHere.h"

#include "Components/StaticMeshComponent.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "EngineUtils.h"
#include "Fragments/Health.h"
#include "Fragments/HealthBar.h"
#include "Fragments/StyleType.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "LandmarkSubsystem.h"
#include "LandmarkSettings.h"
#include "MassBattleEnums.h"
#include "MassBattleStructs.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTypes.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"
#include "Minimap/MapPackageProfilePaths.h"
#include "Renderers/MassBattleAgentRenderer.h"
#include "RTSMoveNavigationProvider.h"
#include "RTSInputPanelSettings.h"
#include "Subsystems/MassBattleNetworkSubsystem.h"

AMassUnitInHere::AMassUnitInHere()
{
	PrimaryActorTick.bCanEverTick = false;

	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	RootComponent = PreviewMeshComponent;
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->SetCastShadow(true);
}

EUnitHereScalePreset AMassUnitInHere::GetResolvedScalePreset() const
{
    if (ScalePreset != EUnitHereScalePreset::Automatic || !AgentConfig)
    {
        return ScalePreset;
    }
    // Legacy infantry assets are not all registered in the RTS protocol yet.
    // Their existing canonical content family is authoritative, not their proxy SubType.
    if (AgentConfig->GetPathName().StartsWith(TEXT("/Game/Unit/Actor/Army/Infantry/")))
    {
        return EUnitHereScalePreset::Infantry;
    }
    const FRTSMassUnitTypeProtocol* Protocol = RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(
        RTSUnitTypeProtocol::GetSettings(), FName(*AgentConfig->GetPathName()), AgentConfig->SubType.Index);
    if (!Protocol)
    {
        return EUnitHereScalePreset::Automatic;
    }
    const FName ClassName = Protocol->UnitTypeTag.GetTagName();
    if (ClassName == TEXT("RTS.UnitClass.Infantry") || ClassName == TEXT("RTS.UnitClass.Officer"))
    {
        return EUnitHereScalePreset::Infantry;
    }
    if (ClassName == TEXT("RTS.UnitClass.Air"))
    {
        return EUnitHereScalePreset::Aircraft;
    }
    if (ClassName == TEXT("RTS.UnitClass.Naval"))
    {
        return EUnitHereScalePreset::Ship;
    }
    if (ClassName == TEXT("RTS.UnitClass.Armor"))
    {
        // Same canonical tank family used by RTSUnitTypeProtocol defaults.
        return Protocol->UnitAssetPath.StartsWith(TEXT("/Game/Unit/Actor/Army/Tank/"))
            ? EUnitHereScalePreset::Tank : EUnitHereScalePreset::Vehicle;
    }
    return EUnitHereScalePreset::Automatic;
}

double AMassUnitInHere::GetResolvedScaleFactor() const
{
    switch (GetResolvedScalePreset())
    {
    case EUnitHereScalePreset::Infantry: return 16.0;
    case EUnitHereScalePreset::Vehicle: return 8.0;
    case EUnitHereScalePreset::Tank: return 4.0;
    case EUnitHereScalePreset::Aircraft: return 2.0 * FMath::Sqrt(2.0);
    case EUnitHereScalePreset::Ship: return 2.0;
    case EUnitHereScalePreset::Custom: return ScaleFactor;
    default: return 0.0;
    }
}

int32 AMassUnitInHere::GetResolvedScaleIterations() const
{
    return static_cast<int32>(bOverrideScaleLevel
        ? ScaleLevel : ULandmarkSettings::Get()->UnitHereDefaultScaleLevel);
}

int32 AMassUnitInHere::GetResolvedQuantity() const
{
    if (!bUseSourceQuantity)
    {
        return FMath::Max(1, Quantity);
    }
    const double Factor = GetResolvedScaleFactor();
    int32 Iterations = GetResolvedScaleIterations();
    if (SourceQuantity < 0 || !FMath::IsFinite(Factor) || Factor < 1.0
        || Iterations < 0 || Iterations > static_cast<int32>(EUnitHereScaleLevel::Division))
    {
        return -1;
    }
    if (SourceQuantity == 0)
    {
        return 0;
    }
    // Integer presets use exact ceiling division, including int64 source inputs.
    // Aircraft pairs of levels are exactly 8:1; do not round sqrt(2) first.
    double IntegerFactor = Factor;
    if (GetResolvedScalePreset() == EUnitHereScalePreset::Aircraft && Iterations % 2 == 0)
    {
        IntegerFactor = 8.0;
        Iterations /= 2;
    }
    if (IntegerFactor <= MAX_int32 && IntegerFactor == FMath::FloorToDouble(IntegerFactor))
    {
        const int64 Divisor = static_cast<int64>(IntegerFactor);
        int64 Result = SourceQuantity;
        for (int32 Index = 0; Index < Iterations; ++Index)
        {
            Result = Result / Divisor + (Result % Divisor != 0 ? 1 : 0);
        }
        return Result <= MAX_int32 ? static_cast<int32>(Result) : -1;
    }
    const double Divisor = FMath::Pow(Factor, static_cast<double>(Iterations));
    const double Result = FMath::CeilToDouble(static_cast<double>(SourceQuantity) / Divisor);
    return Result <= MAX_int32 ? FMath::Max(1, static_cast<int32>(Result)) : -1;
}

void AMassUnitInHere::RefreshQuantity()
{
    if (bUseSourceQuantity)
    {
        Quantity = GetResolvedQuantity();
    }
}

void AMassUnitInHere::PostLoad()
{
    Super::PostLoad();
    RefreshQuantity();
}

void AMassUnitInHere::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshQuantity();
	UpdatePreview();
}

void AMassUnitInHere::UpdatePreview()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	if (!AgentConfig)
	{
		PreviewMeshComponent->SetStaticMesh(nullptr);
		return;
	}

	TSubclassOf<AMassBattleAgentRenderer> RendererClass = AgentConfig->Visualize.RendererClass.LoadSynchronous();
	if (!RendererClass)
	{
		PreviewMeshComponent->SetStaticMesh(nullptr);
		return;
	}

	if (const AMassBattleAgentRenderer* CDO = RendererClass->GetDefaultObject<AMassBattleAgentRenderer>())
	{
		PreviewMeshComponent->SetStaticMesh(CDO->AgentMesh);
	}
}

bool AMassUnitInHere::IsConfiguredCityUnit() const
{
	if (!AgentConfig)
	{
		return false;
	}

	const ULandmarkSettings* Settings = ULandmarkSettings::Get();
	if (!Settings)
	{
		return false;
	}

	const FSoftObjectPath AgentPath(AgentConfig);
	for (const FCityLevelConfig& CityConfig : Settings->CityLevelConfigs)
	{
		if (CityConfig.MassConfig.Get() == AgentConfig
			|| CityConfig.MassConfig.ToSoftObjectPath() == AgentPath)
		{
			return true;
		}
	}
	return false;
}

int32 AMassUnitInHere::ResolveMapFlagSetIndex() const
{
	const UWorld* World = GetWorld();
	const ULandmarkSettings* Settings = ULandmarkSettings::Get();
	if (!World || !Settings)
	{
		return 0;
	}

	const FString MapPackagePath =
		MassBattleMapProfilePaths::GetCanonicalMapPackagePath(World);
	const FLandmarkMapProfile* MapProfile = Settings->FindMapProfile(MapPackagePath);
	return MapProfile ? FMath::Max(0, MapProfile->FlagSetIndex) : 0;
}

void AMassUnitInHere::BeginPlay()
{
	Super::BeginPlay();
	if (!bLevelUnitsInitialized)
	{
		InitializeLevelUnits(false);
	}
}

int32 AMassUnitInHere::InitializeAllLevelUnits(UWorld& World)
{
	TArray<AMassUnitInHere*> Placements;
	for (TActorIterator<AMassUnitInHere> It(&World); It; ++It)
	{
		if (It->bSpawnEnabled && !It->IsTemplate())
		{
			Placements.Add(*It);
		}
	}
	Placements.Sort([](const AMassUnitInHere& A, const AMassUnitInHere& B)
	{
		return A.GetFName().LexicalLess(B.GetFName());
	});

	UMassBattleNetworkSubsystem* Network =
		World.GetSubsystem<UMassBattleNetworkSubsystem>();
	if (!Network)
	{
		UE_LOG(LogTemp, Error,
			TEXT("MassUnitInHere level initialization has no MassBattle network subsystem."));
		return 0;
	}
	if (!Network->BeginLevelInitialization())
	{
		return 0;
	}
	ON_SCOPE_EXIT
	{
		Network->EndLevelInitialization();
	};

	int32 CompletedFormations = 0;
	int64 CompletedEntities = 0;
	for (AMassUnitInHere* Placement : Placements)
	{
		const int32 PlacementQuantity = Placement
			? Placement->GetResolvedQuantity()
			: 0;
		if (Placement && Placement->InitializeLevelUnits(true))
		{
			++CompletedFormations;
			CompletedEntities += PlacementQuantity;
		}
	}
	// Project systems may add deterministic map-authored entities (for example
	// national government objectives) only while this shared Tick-0 scope is
	// still active. Runtime synchronous spawning is intentionally rejected in
	// network play outside the level-initialization or command scope.
	const bool bAllPlacementsCompleted =
		CompletedFormations == Placements.Num();
	if (bAllPlacementsCompleted)
	{
		if (ULandmarkSubsystem* Landmarks =
			World.GetSubsystem<ULandmarkSubsystem>())
		{
			Landmarks->NotifyMapInitializationReady();
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("MassUnitInHere LEVEL_INIT failed closed: only %d/%d formations completed; Tick 0 will not be released as map-ready."),
			CompletedFormations,
			Placements.Num());
	}
	UE_LOG(LogTemp, Display,
		TEXT("MassUnitInHere LEVEL_INIT completed %d/%d formations (%lld entities) before network simulation."),
		CompletedFormations,
		Placements.Num(),
		static_cast<long long>(CompletedEntities));
	return CompletedFormations;
}

bool AMassUnitInHere::InitializeLevelUnits(const bool bForceSynchronous)
{
	if (bLevelUnitsInitialized)
	{
		return true;
	}

	if (!bSpawnEnabled)
	{
		UE_LOG(LogTemp, Log, TEXT("MassUnitInHere [%s] spawn disabled; skipping Mass spawn."), *GetName());
		Destroy();
		bLevelUnitsInitialized = true;
		return true;
	}

	UWorld* World = GetWorld();
	if (!World || !AgentConfig)
	{
		Destroy();
		return false;
	}

	// Network peers must reach this actor through InitializeAllLevelUnits while
	// the Tick-0 scope is active. BeginPlay is only a standalone/fallback path.
	if (!bForceSynchronous && World->GetNetMode() != NM_Standalone)
	{
		if (PreviewMeshComponent)
		{
			PreviewMeshComponent->SetHiddenInGame(true);
		}
		UE_LOG(LogTemp, Error,
			TEXT("MassUnitInHere [%s] missed Tick-0 level initialization; runtime spawn rejected."),
			*GetName());
		return false;
	}
	const int32 SafeQuantity = GetResolvedQuantity();
	if (SafeQuantity < 0)
	{
		UE_LOG(LogTemp, Error, TEXT("MassUnitInHere [%s] invalid source quantity, coefficient or scale level."), *GetName());
		return false;
	}
	bLevelUnitsInitialized = true;
	if (SafeQuantity == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("MassUnitInHere [%s] level spawn completed: Spawned=0 Team=%d."), *GetName(), Team);
		Destroy();
		return true;
	}

	FAgentSpawnRectangleShapeData Shape;
	const float SideCount = FMath::CeilToFloat(FMath::Sqrt(static_cast<float>(SafeQuantity)));
	// A one-unit placement actor is a point placement, not a one-cell formation.
	// A Region equal to Spacing produces four candidates at +/-Spacing/2 and the
	// Mass spawner chooses one of them, so repeated UnitHere runs visibly jitter.
	// An invalid/zero region follows MassBattleFrame's documented origin path.
	const float RegionSize = SafeQuantity == 1
		? 0.0f
		: FMath::Max(SpawnSpacing, (SideCount - 1.0f) * SpawnSpacing);
	Shape.Region = FVector2D(RegionSize, RegionSize);
	Shape.Spacing = FVector2D(SpawnSpacing, SpawnSpacing);

	FVector SpawnLocation = GetActorLocation();
	const float FormationRadiusUU = Shape.Region.Size() * 0.5f;
	if (!FRTSMoveNavigationProviderRegistry::ResolveInitialSpawnLocation(
			this,
			AgentConfig,
			SpawnLocation,
			FormationRadiusUU,
			SpawnLocation))
	{
		if (PreviewMeshComponent)
		{
			PreviewMeshComponent->SetHiddenInGame(true);
		}
		UE_LOG(LogTemp, Error,
			TEXT("MassUnitInHere [%s] rejected: no legal terrain-domain location exists for its complete formation."),
			*GetName());
		return false;
	}

	if (!bForceSynchronous && bDeferLargeSpawns
		&& SafeQuantity > FMath::Max(1, AgentsPerSpawnStep))
	{
		const int32 SpawnStepCount = FMath::CeilToInt(
			static_cast<float>(SafeQuantity) / static_cast<float>(FMath::Max(1, AgentsPerSpawnStep)));
		const int32 Substeps = FMath::Max(1, SpawnStepCount - 1);

		FOnAgentSpawnFinished OnFinished;
		OnFinished.BindDynamic(this, &AMassUnitInHere::HandleDeferredSpawnFinished);
		UMassBattleFuncLib::SpawnAgentsByConfigRectangularDeferred(
			this,
			AgentConfig,
			SafeQuantity,
			Team,
			SpawnLocation,
			Shape,
			FVector2D::ZeroVector,
			EInitialRotation::CustomRotation,
			GetActorRotation(),
			FSpawnerMult(),
			true,
			Substeps,
			OnFinished);

		UE_LOG(LogTemp, Log,
			TEXT("MassUnitInHere [%s] deferred spawn submitted: Quantity=%d Team=%d Steps=%d."),
			*GetName(), SafeQuantity, Team, SpawnStepCount);
		return true;
	}

	const TArray<FEntityHandle> SpawnedEntities = UMassBattleFuncLib::SpawnAgentsByConfigRectangular(
		this,
		AgentConfig,
		SafeQuantity,
		Team,
		SpawnLocation,
		Shape,
		FVector2D::ZeroVector,
		EInitialRotation::CustomRotation,
		GetActorRotation());

	ApplySpawnOverrides(SpawnedEntities);
	UE_LOG(LogTemp, Display,
		TEXT("MassUnitInHere [%s] level spawn completed: Spawned=%d Team=%d."),
		*GetName(), SpawnedEntities.Num(), Team);
	Destroy();
	return SpawnedEntities.Num() == SafeQuantity;
}

void AMassUnitInHere::HandleDeferredSpawnFinished(const TArray<FEntityHandle>& SpawnedEntities)
{
	ApplySpawnOverrides(SpawnedEntities);
	UE_LOG(LogTemp, Log,
		TEXT("MassUnitInHere [%s] deferred spawn completed: Spawned=%d Team=%d."),
		*GetName(), SpawnedEntities.Num(), Team);
	Destroy();
}

void AMassUnitInHere::ApplySpawnOverrides(const TArray<FEntityHandle>& SpawnedEntities)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const bool bCityUnit = IsConfiguredCityUnit();
	if ((bCityUnit || HealthOverride > 0.f || bOverrideHealthBarVisibility)
		&& SpawnedEntities.Num() > 0)
	{
		if (UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>())
		{
			FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
			const int32 FlagSetIndex = bCityUnit ? ResolveMapFlagSetIndex() : 0;
			for (const FEntityHandle& Handle : SpawnedEntities)
			{
				if (bCityUnit)
				{
					if (FStyleType* Style = EntityManager.GetFragmentDataPtr<FStyleType>(Handle))
					{
						Style->Variant = FlagSetIndex;
					}
				}

				if (HealthOverride > 0.f)
				{
					if (FHealth* HealthFragment = EntityManager.GetFragmentDataPtr<FHealth>(Handle))
					{
						HealthFragment->Maximum = HealthOverride;
						HealthFragment->Current = HealthOverride;
					}
				}

				if (bOverrideHealthBarVisibility)
				{
					if (FHealthBar* HealthBarFragment = EntityManager.GetFragmentDataPtr<FHealthBar>(Handle))
					{
						HealthBarFragment->bShowHealthBar = bShowHealthBarByDefault;
						HealthBarFragment->bShowOnSelected = bShowHealthBarOnSelected;
						HealthBarFragment->HideOnFullHealth = true;
						HealthBarFragment->Opacity = 0.0f;
					}
				}
			}
		}
	}
}
