#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LandmarkTypes.h"
#include "Components/BillboardComponent.h"
#include "Components/TextRenderComponent.h"
#include "LandmarkMapLabelProxy.generated.h"

/**
 * Editor-only actor for placing landmarks.
 * - Updates JSON when saved (via Editor Utility - separate tool).
 * - Updates Visuals in editor.
 * - Stripped in build (bIsEditorOnlyActor = true).
 */
UCLASS()
class LANDMARKSYSTEM_API ALandmarkMapLabelProxy : public AActor
{
	GENERATED_BODY()
	
public:	
	ALandmarkMapLabelProxy();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FString ID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	ELandmarkType Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FLandmarkVisualConfig VisualConfig;

	// Visual Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	TObjectPtr<USceneComponent> SceneComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	TObjectPtr<UBillboardComponent> Icon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	TObjectPtr<UTextRenderComponent> LabelText;

public:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Snaps the actor to the ground using a line trace */
	UFUNCTION(CallInEditor, Category = "Landmark")
	void SnapToGround();
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdateVisuals();
};
