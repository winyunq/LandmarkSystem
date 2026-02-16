#include "LandmarkCloudComponent.h"
#include "LandmarkSubsystem.h"

ULandmarkCloudComponent::ULandmarkCloudComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULandmarkCloudComponent::BeginPlay()
{
	Super::BeginPlay();

    if (Landmarks.Num() == 0) return;

    if (ULandmarkSubsystem* Subsystem = GetWorld()->GetSubsystem<ULandmarkSubsystem>())
    {
        for (const FLandmarkInstanceData& Data : Landmarks)
        {
            // Transform local location to world location if needed
            // Data.WorldLocation is stored as world location? 
            // Usually simpler if stored as Local Offset, but for "Map Labels" absolute world coordinates often preferred.
            // Let's assume Data.WorldLocation IS World Location.
            // If the user moves the CloudComponent, do points move?
            // "Cloud" usually implies points are relative to the component?
            // If points are absolute, moving component does nothing.
            // Let's support Relative. But `FLandmarkInstanceData` has `WorldLocation`.
            // Decision: If we are editing "Map Points", usually we want them absolute.
            // But SceneComponents usually imply hierarchy.
            // If the Visualizer writes to `Data.WorldLocation`, then moving the component transform changes nothing?
            // Better behavior: Store as Relative, Register as World.
            // But `FLandmarkInstanceData` struct is shared and used by everything as "WorldLocation".
            // Let's modify registration: Register(Data) expects WorldLocation.
            // If stored data is also deemed "WorldLocation" (e.g. imported from GIS), we just pass it.
            // If we want hierarchy support, we should add `GetComponentTransform().TransformPosition(Data.Location)`.
            // For now, let's assume Data.WorldLocation is ABSOLUTE.
            
            Subsystem->RegisterLandmark(Data);
        }
    }

    // Optimization: We don't need to exist anymore after dumping data.
    DestroyComponent();
}

void ULandmarkCloudComponent::ImportLandmarks(const TArray<FLandmarkInstanceData>& InLandmarks, bool bAppend)
{
    if (!bAppend)
    {
        Landmarks.Empty();
    }
    Landmarks.Append(InLandmarks);
}

void ULandmarkCloudComponent::ClearLandmarks()
{
    Landmarks.Empty();
}
