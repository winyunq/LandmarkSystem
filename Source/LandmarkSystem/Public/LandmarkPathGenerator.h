#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LandmarkTypes.h"
#include "Components/SplineComponent.h"
#include "LandmarkPathGenerator.generated.h"

/**
 * Procedurally generates visible landmarks along a spline path.
 * Usage: Rivers, Trade Routes, Borders.
 * - Registers generated points with LandmarkSubsystem at runtime.
 */
UCLASS()
class LANDMARKSYSTEM_API ALandmarkPathGenerator : public AActor
{
	GENERATED_BODY()
	
public:	
	ALandmarkPathGenerator();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	TObjectPtr<USplineComponent> SplinePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FString BaseID = "PathLandmark";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FText BaseDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	float Spacing = 5000.0f; // Distance between landmarks

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	ELandmarkType Type = ELandmarkType::River;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FLandmarkVisualConfig VisualConfig;

protected:
	virtual void BeginPlay() override;

private:
	void GenerateLandmarks();
};
