#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LandmarkCloudComponent.h"
#include "LandmarkCloudActor.generated.h"

/**
 * A dedicated Actor for placing a Cloud of Landmarks.
 * Simply wraps a ULandmarkCloudComponent for easier placement in the level.
 */
UCLASS()
class LANDMARKSYSTEM_API ALandmarkCloudActor : public AActor
{
	GENERATED_BODY()
	
public:	
	ALandmarkCloudActor();

    /** The component that holds the data and handles visualization */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
    TObjectPtr<ULandmarkCloudComponent> CloudComponent;

protected:
    // This actor is just a wrapper, no extra logic needed.
};
