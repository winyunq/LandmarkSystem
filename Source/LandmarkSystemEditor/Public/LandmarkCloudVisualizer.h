#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"
#include "LandmarkCloudComponent.h"

/**
 * Visualizer for ULandmarkCloudComponent.
 * Allows editing landmark points directly in the viewport.
 */
class FLandmarkCloudVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;

protected:
	/** Index of the currently selected point in the Landmarks array */
	int32 SelectedPointIndex = INDEX_NONE;

    /** Property Path to the component being edited */
    FComponentPropertyPath PropertyPath;
};
