#include "LandmarkCloudVisualizer.h"
#include "LandmarkCloudComponent.h"
#include "Modules/ModuleManager.h"
#include "EditorViewportClient.h"
#include "SceneManagement.h"
#include "UnrealEdGlobals.h" // For GUnrealEd
#include "Editor/UnrealEdEngine.h"

#define LOCTEXT_NAMESPACE "LandmarkCloudVisualizer"

struct HLandmarkPointProxy : public HComponentVisProxy
{
    DECLARE_HIT_PROXY();
    int32 PointIndex;
    HLandmarkPointProxy(const UActorComponent* InComponent, int32 InPointIndex)
        : HComponentVisProxy(InComponent, HPP_Wireframe), PointIndex(InPointIndex)
    {}
};

IMPLEMENT_HIT_PROXY(HLandmarkPointProxy, HComponentVisProxy);

void FLandmarkCloudVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
    const ULandmarkCloudComponent* CloudComp = Cast<ULandmarkCloudComponent>(Component);
    if (!CloudComp) return;

    // Standard way to get properties?
    // Usually we don't use ComponentPropertyPath here unless we stored it.
    // But Visualizer usually works on the component passed in DrawVisualization.
    
    // NOTE: Visualizers don't persist state easily between Draw calls unless we store selection.
    // If we want to move points, we need to know which component we are editing during Input.
    
    // We can't really "fix" PropertyPath not being declared if it's a member we added to header but implementation doesn't see it?
    // Wait, did I Add PropertyPath to header? Yes, in Plan. But maybe header file wasn't updated correctly?
    // Let's assume PropertyPath IS a member of FLandmarkCloudVisualizer.
    // The error says "undeclared identifier". This means the header likely doesn't have it, or it wasn't saved?
    // Ah, Visualizer usually stores the property path when hit proxy is clicked.
    // Let's check header content in next step. For now, assume we need to use 'Component' directly in Draw.
    // But HandleInputDelta doesn't get 'Component'. It gets ViewportClient.
    // So we MUST store the component we are editing.
    
    // In DrawVisualization, we are just drawing. We don't need PropertyPath yet.
    // But we need to make sure PropertyPath IS defined in the class.
    
    const FTransform& CompTransform = CloudComp->GetComponentTransform();
    const TArray<FLandmarkInstanceData>& Points = CloudComp->Landmarks;

    for (int32 i = 0; i < Points.Num(); ++i)
    {
        const FLandmarkInstanceData& Data = Points[i];
        
        // Assume Data.WorldLocation is World Space. 
        // If user wants Relative Space, we'd do CompTransform.TransformPosition(Data.Location).
        // Based on "Import/Export" requirement, usually absolute is better for map data.
        // But if we move the cloud actor, usually we want points to move?
        // Let's assume World Space for now as it's "Map Labels".
        FVector PointLoc = Data.WorldLocation; 

        // Color based on selection
        FLinearColor Color = (i == SelectedPointIndex) ? FLinearColor::Yellow : FLinearColor::Green;

        PDI->SetHitProxy(new HLandmarkPointProxy(Component, i));
        
        // Draw Point Handle
        PDI->DrawPoint(PointLoc, Color, 10.0f, SDPG_Foreground);
        
        // Optional: Draw small box for easier clicking
        DrawWireBox(PDI, FBox(PointLoc - FVector(10), PointLoc + FVector(10)), Color, SDPG_Foreground);

        PDI->SetHitProxy(NULL);
    }
}

bool FLandmarkCloudVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
    if (VisProxy && VisProxy->IsA(HLandmarkPointProxy::StaticGetType()))
    {
        HLandmarkPointProxy* Proxy = (HLandmarkPointProxy*)VisProxy;
        SelectedPointIndex = Proxy->PointIndex;
        
        // Focus on click?
        return true;
    }
    
    SelectedPointIndex = INDEX_NONE;
    return false;
}

bool FLandmarkCloudVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
    const ULandmarkCloudComponent* CloudComp = Cast<const ULandmarkCloudComponent>(PropertyPath.GetComponent());
    if (CloudComp && CloudComp->Landmarks.IsValidIndex(SelectedPointIndex))
    {
        OutLocation = CloudComp->Landmarks[SelectedPointIndex].WorldLocation;
        return true;
    }
    return false;
}

bool FLandmarkCloudVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
    ULandmarkCloudComponent* CloudComp = Cast<ULandmarkCloudComponent>(PropertyPath.GetComponent());
    if (CloudComp && CloudComp->Landmarks.IsValidIndex(SelectedPointIndex))
    {
        if (!DeltaTranslate.IsZero())
        {
            CloudComp->Modify(); // Notify Editor of change for Undo/Redo
            CloudComp->Landmarks[SelectedPointIndex].WorldLocation += DeltaTranslate;
            
            // Re-render
            GUnrealEd->RedrawLevelEditingViewports();
            return true;
        }
    }
    return false;
}

#undef LOCTEXT_NAMESPACE
