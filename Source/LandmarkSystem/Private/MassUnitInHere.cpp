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

	TSubclassOf<AMassBattleAgentRenderer> RendererClass = AgentConfig->Render.RendererClass.LoadSynchronous();
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

	UWorld* World = GetWorld();
	if (!World || !AgentConfig)
	{
		Destroy();
		return;
	}

	const int32 SafeQuantity = FMath::Max(1, Quantity);

	FAgentSpawnRectangleShapeData Shape;
	const float SideCount = FMath::CeilToFloat(FMath::Sqrt(static_cast<float>(SafeQuantity)));
	const float RegionSize = FMath::Max(SpawnSpacing, (SideCount - 1.0f) * SpawnSpacing);
	Shape.Region = FVector2D(RegionSize, RegionSize);
	Shape.Spacing = FVector2D(SpawnSpacing, SpawnSpacing);

	TArray<FEntityHandle> SpawnedEntities = UMassBattleFuncLib::SpawnAgentsByConfigRectangular(
		this,
		AgentConfig,
		SafeQuantity,
		Team,
		GetActorLocation(),
		Shape,
		FVector2D::ZeroVector,
		EInitialRotation::CustomRotation,
		GetActorRotation()
	);

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

	Destroy();
}
