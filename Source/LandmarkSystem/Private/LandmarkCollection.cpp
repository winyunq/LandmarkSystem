#include "LandmarkCollection.h"

ALandmarkCollection::ALandmarkCollection()
{
	PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
}

void ALandmarkCollection::BeginPlay()
{
	Super::BeginPlay();
	// The Components (ULandmarkComponent) attached to this actor 
    // will handle their own registration in their BeginPlay.
    // This actor is just a container.
}
