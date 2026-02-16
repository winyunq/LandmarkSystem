#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LandmarkTypes.h"
#include "LandmarkComponent.generated.h"

// Forward declarations
class UBillboardComponent;
class UTextRenderComponent;

/**
 * A component representing a single landmark.
 * - Can be added to any Actor (e.g. ALandmarkCollection).
 * - Draggable independently in Editor viewport.
 * - Registers to Subsystem at runtime and then (optionally) deactivates itself to save performance.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class LANDMARKSYSTEM_API ULandmarkComponent : public USceneComponent
{
	GENERATED_BODY()

public:	
	ULandmarkComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FString ID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	ELandmarkType Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
	FLandmarkVisualConfig VisualConfig;

	// --- Editor Visualization ---
    // We treat these as transient editor-only helpers
#if WITH_EDITORONLY_DATA
    UPROPERTY(Transient)
    TObjectPtr<UBillboardComponent> SpriteComponent;

    UPROPERTY(Transient)
    TObjectPtr<UTextRenderComponent> TextComponent;
#endif

protected:
	virtual void BeginPlay() override;
    virtual void OnRegister() override; // Used to setup editor visuals

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    void UpdateEditorVisuals();
    void EnsureEditorComponents();
#endif
};
