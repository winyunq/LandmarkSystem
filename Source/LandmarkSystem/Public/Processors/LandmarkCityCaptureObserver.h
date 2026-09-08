#pragma once

#include "MassEntityQuery.h"
#include "MassObserverProcessor.h"
#include "LandmarkCityCaptureObserver.generated.h"

/** Converts a city entering the dying state into one event-driven ownership transfer. */
UCLASS()
class LANDMARKSYSTEM_API ULandmarkCityCaptureObserver final
	: public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	ULandmarkCityCaptureObserver();

protected:
	virtual void ConfigureQueries(
		const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(
		FMassEntityManager& EntityManager,
		FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
