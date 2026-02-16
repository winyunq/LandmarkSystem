#include "LandmarkSystemEditorModule.h"
#include "LandmarkCloudComponent.h"
#include "LandmarkCloudVisualizer.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

void FLandmarkSystemEditorModule::StartupModule()
{
    RegisterComponentVisualizer();
}

void FLandmarkSystemEditorModule::ShutdownModule()
{
    if (GUnrealEd)
    {
        GUnrealEd->UnregisterComponentVisualizer(ULandmarkCloudComponent::StaticClass()->GetFName());
    }
}

void FLandmarkSystemEditorModule::RegisterComponentVisualizer()
{
    if (GUnrealEd)
    {
        TSharedPtr<FLandmarkCloudVisualizer> Visualizer = MakeShareable(new FLandmarkCloudVisualizer);
        if (Visualizer.IsValid())
        {
            GUnrealEd->RegisterComponentVisualizer(ULandmarkCloudComponent::StaticClass()->GetFName(), Visualizer);
            Visualizer->OnRegister(); 
        }
    }
}

IMPLEMENT_MODULE(FLandmarkSystemEditorModule, LandmarkSystemEditor)
