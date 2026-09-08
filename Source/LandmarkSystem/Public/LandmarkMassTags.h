#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "LandmarkMassTags.generated.h"

/** Marks the persistent gameplay entity that owns one city's capture state. */
USTRUCT()
struct LANDMARKSYSTEM_API FCityLandmarkTag : public FMassTag
{
	GENERATED_BODY()
};
