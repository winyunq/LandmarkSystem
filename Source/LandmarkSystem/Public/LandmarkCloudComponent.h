#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LandmarkTypes.h"
#include "LandmarkCloudComponent.generated.h"

/**
 * A data container for multiple landmarks.
 * - Stores a list of landmarks in a simple array (Matrix).
 * - Visualized and edited in-place via a Component Visualizer (Editor Module).
 * - Registers all points to the Subsystem at runtime, then becomes dormant.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class LANDMARKSYSTEM_API ULandmarkCloudComponent : public USceneComponent
{
	GENERATED_BODY()

public:	
	ULandmarkCloudComponent();

	/** The raw data matrix. Edited via Visualizer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark Cloud")
	TArray<FLandmarkInstanceData> Landmarks;

    /** Helper to bulk add points (e.g. from Python or Blueprint import) */
    UFUNCTION(BlueprintCallable, Category = "Landmark Cloud")
    void ImportLandmarks(const TArray<FLandmarkInstanceData>& InLandmarks, bool bAppend = false);
    
    /** Helper to clear all points */
    UFUNCTION(BlueprintCallable, Category = "Landmark Cloud")
    void ClearLandmarks();

protected:
	virtual void BeginPlay() override;
};
