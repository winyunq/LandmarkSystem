// Copyright 2026 Winyunq. All Rights Reserved.

#include "MassUnitInHere.h"

#include "Components/StaticMeshComponent.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "Fragments/Health.h"
#include "Fragments/HealthBar.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "MassBattleEnums.h"
#include "MassBattleStructs.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTypes.h"
#include "Renderers/MassBattleAgentRenderer.h"

AMassUnitInHere::AMassUnitInHere()
{
	PrimaryActorTick.bCanEverTick = false;

	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	RootComponent = PreviewMeshComponent;
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->SetCastShadow(true);
}

void AMassUnitInHere::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
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

void AMassUnitInHere::BeginPlay()
{
	Super::BeginPlay();

	if (!bSpawnEnabled)
	{
		UE_LOG(LogTemp, Log, TEXT("MassUnitInHere [%s] spawn disabled; skipping Mass spawn."), *GetName());
		Destroy();
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !AgentConfig)
	{
		Destroy();
		return;
	}

	const int32 SafeQuantity = FMath::Max(1, Quantity);

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

	if (bDeferLargeSpawns && SafeQuantity > FMath::Max(1, AgentsPerSpawnStep))
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
			GetActorLocation(),
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
		return;
	}

	const TArray<FEntityHandle> SpawnedEntities = UMassBattleFuncLib::SpawnAgentsByConfigRectangular(
		this,
		AgentConfig,
		SafeQuantity,
		Team,
		GetActorLocation(),
		Shape,
		FVector2D::ZeroVector,
		EInitialRotation::CustomRotation,
		GetActorRotation());

	ApplySpawnOverrides(SpawnedEntities);
	Destroy();
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

	if ((HealthOverride > 0.f || bOverrideHealthBarVisibility) && SpawnedEntities.Num() > 0)
	{
		if (UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>())
		{
			FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
			for (const FEntityHandle& Handle : SpawnedEntities)
			{
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
