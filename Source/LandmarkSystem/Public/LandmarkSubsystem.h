#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LandmarkTypes.h"
#include "LandmarkSubsystem.generated.h"

/**
 * ULandmarkSubsystem
 * 
 * Central manager for the Landmark System.
 * - Manages registration of landmarks from various sources (JSON, Actors, Splines).
 * - Filters visible landmarks based on Camera View.
 * - Calculates adaptive scaling (Anti-intuitive scaling).
 * - Provides draw data for HUD/Canvas.
 */
UCLASS()
class LANDMARKSYSTEM_API ULandmarkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Registration API ---

	/** Register a new landmark. Returns a generated ID if input ID is empty */
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void RegisterLandmark(const FLandmarkInstanceData& Data);

	/** Update an existing landmark's data */
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UpdateLandmark(const FString& ID, const FLandmarkInstanceData& NewData);

	/** Unregister a landmark */
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UnregisterLandmark(const FString& ID);

	/** Clear all landmarks */
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UnregisterAll();

    // --- File I/O ---

    /** Load landmarks from a JSON file (Relative to Project Content/MapData/) */
    UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
    bool LoadLandmarksFromFile(const FString& FileName);

    /** Save landmarks to a JSON file (Relative to Project Content/MapData/) */
    UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
    bool SaveLandmarksToFile(const FString& FileName, const TArray<FLandmarkInstanceData>& DataToSave);

	// --- Runtime API ---

	/** 
	 * Call this every frame (or when camera moves) to update visibility and scaling 
	 * @param CameraLocation - World location of the camera
	 * @param CameraRotation - World rotation of the camera
	 * @param FOV - Field of view
	 * @param ZoomFactor - 0.0 (Ground) to 1.0 (Space). Used for adaptive scaling.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UpdateCameraState(const FVector& CameraLocation, const FRotator& CameraRotation, float FOV, float ZoomFactor);

	/**
	 * Helper to project world to screen and get render data.
	 * Can be called by HUD to draw.
	 */
	void GetVisibleLandmarks(TArray<FLandmarkInstanceData>& OutVisibleLandmarks, TArray<FVector2D>& OutScreenPositions, TArray<float>& OutScales, TArray<float>& OutAlphas);

	// --- Configuration ---

	/** Curve defining Scale vs ZoomFactor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandmarkSystem")
	FRuntimeFloatCurve ScaleCurve;

	/** Curve defining Opacity vs ZoomFactor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandmarkSystem")
	FRuntimeFloatCurve AlphaCurve;

protected:
	
	/** Main storage */
	UPROPERTY()
	TMap<FString, FLandmarkInstanceData> RegisteredLandmarks;

	/** Cached visible set from last UpdateCameraState */
	TArray<FString> VisibleLandmarkIDs;
	TArray<FVector2D> CachedScreenPositions;
	TArray<float> CachedScales;
	TArray<float> CachedAlphas;

	/** Camera state cache */
	FVector LastCameraLoc;
	FRotator LastCameraRot;
	float LastZoomFactor = 0.5f;

	/** Internal helper to project */
	bool ProjectWorldLocationToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const;
};
