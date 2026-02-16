#include "LandmarkCloudActor.h"

ALandmarkCloudActor::ALandmarkCloudActor()
{
	PrimaryActorTick.bCanEverTick = false;

    CloudComponent = CreateDefaultSubobject<ULandmarkCloudComponent>(TEXT("CloudComponent"));
    RootComponent = CloudComponent;
}
