#include "Processors/LandmarkCityCaptureObserver.h"

#include "Fragments/Death.h"
#include "Fragments/Health.h"
#include "Fragments/Team.h"
#include "Engine/World.h"
#include "LandmarkMassTags.h"
#include "LandmarkSubsystem.h"
#include "MassAPISubsystem.h"
#include "MassExecutionContext.h"
#include "TimerManager.h"

ULandmarkCityCaptureObserver::ULandmarkCityCaptureObserver()
	: EntityQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	bAutoRegisterWithProcessingPhases = false;
	bRequiresGameThreadExecution = true;
	ObservedTypes.Add(FDyingTag::StaticStruct());
	ObservedOperations = EMassObservedOperationFlags::Add;
}

void ULandmarkCityCaptureObserver::ConfigureQueries(
	const TSharedRef<FMassEntityManager>& EntityManager)
{
	FEntityQueryBuilder(EntityQuery)
		.All<FCityLandmarkTag, FDyingTag>()
		.All<FHealth, FDying>(MARW)
		.RegisterWithProcessor(*this);
}

void ULandmarkCityCaptureObserver::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	ULandmarkSubsystem* Landmarks = World
		? World->GetSubsystem<ULandmarkSubsystem>()
		: nullptr;
	if (!Landmarks)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(
		Context,
		[Landmarks, World, &EntityManager](FMassExecutionContext& ChunkContext)
		{
			const TArrayView<FHealth> Health =
				ChunkContext.GetMutableFragmentView<FHealth>();
			const TArrayView<FDying> Dying =
				ChunkContext.GetMutableFragmentView<FDying>();
			for (FMassExecutionContext::FEntityIterator EntityIt =
				ChunkContext.CreateEntityIterator(); EntityIt; ++EntityIt)
			{
				if (Health[EntityIt].Current > 0.0f)
				{
					continue;
				}
				const FMassEntityHandle Instigator = Dying[EntityIt].Instigator;
				const FTeam* CapturingTeam = EntityManager.IsEntityActive(Instigator)
					? EntityManager.GetFragmentDataPtr<FTeam>(Instigator)
					: nullptr;
				const FMassEntityHandle CityEntity =
					ChunkContext.GetEntity(EntityIt);
				if (CapturingTeam)
				{
					const int32 CapturingTeamIndex = CapturingTeam->index;
					const TWeakObjectPtr<ULandmarkSubsystem> WeakLandmarks(Landmarks);
					World->GetTimerManager().SetTimerForNextTick(
						FTimerDelegate::CreateLambda(
							[WeakLandmarks, CityEntity, CapturingTeamIndex]()
							{
								ULandmarkSubsystem* DeferredLandmarks =
									WeakLandmarks.Get();
								if (!DeferredLandmarks
									|| !DeferredLandmarks->CaptureCityFromLethalDamage(
										FEntityHandle(CityEntity),
										CapturingTeamIndex))
								{
									return;
								}

								if (UMassAPISubsystem* MassAPI =
									UMassAPISubsystem::GetPtr(DeferredLandmarks))
								{
									if (MassAPI->HasTag<FDyingTag>(CityEntity))
									{
										MassAPI->RemoveTag<FDyingTag>(CityEntity);
									}
								}
							}));
				}
			}
		});
}
