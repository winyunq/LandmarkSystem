#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LandmarkCollection.generated.h"

/**
 * A container actor designed to hold multiple ULandmarkComponents.
 * Place this in the world and add "Landmark Component" to it to create groups.
 */
UCLASS()
class LANDMARKSYSTEM_API ALandmarkCollection : public AActor
{
	GENERATED_BODY()
	
public:	
	ALandmarkCollection();

    // Just a root component to hold the children
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
    TObjectPtr<USceneComponent> SceneRoot;

protected:
	virtual void BeginPlay() override;

};
