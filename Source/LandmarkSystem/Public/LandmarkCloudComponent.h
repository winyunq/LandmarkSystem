#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LandmarkTypes.h"
#include "LandmarkCloudComponent.generated.h"

/**
 * A data container for multiple landmarks.
 * - Stores a list of landmarks in a simple array (Matrix).
 * - Visualized and edited in-place via a Component Visualizer (Editor Module).
 * - Acts as a Load/Save bridge for JSON data.
 * - At runtime, it instructs the Subsystem to load the specific JSON file, then self-destructs.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class LANDMARKSYSTEM_API ULandmarkCloudComponent : public USceneComponent
{
	GENERATED_BODY()

public:	
	ULandmarkCloudComponent();

    /** The name of the JSON file to load/save (e.g. "Level1_Landmarks.json") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark IO")
    FString JsonFileName = "DefaultLandmarks.json";

	/** The raw data matrix. Edited via Visualizer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark Cloud")
	TArray<FLandmarkInstanceData> Landmarks;

    /** Helper to bulk add points (e.g. from Python or Blueprint import) */
    UFUNCTION(BlueprintCallable, Category = "Landmark Cloud")
    void ImportLandmarks(const TArray<FLandmarkInstanceData>& InLandmarks, bool bAppend = false);
    
    /** Helper to clear all points */
    UFUNCTION(BlueprintCallable, Category = "Landmark Cloud")
    void ClearLandmarks();

    // --- Editor Tools ---
    
    /** Loads data from JsonFileName into the Landmarks array for editing. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Landmark IO")
    void LoadFromJson();

    /** Saves current Landmarks array to JsonFileName. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Landmark IO")
    void SaveToJson();

protected:
	virtual void BeginPlay() override;
};
