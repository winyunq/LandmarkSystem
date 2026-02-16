#include "LandmarkCloudComponent.h"
#include "LandmarkSubsystem.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

ULandmarkCloudComponent::ULandmarkCloudComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULandmarkCloudComponent::BeginPlay()
{
	Super::BeginPlay();

    // Runtime Logic:
    // 1. Tell Subsystem to load the file associated with this component
    if (ULandmarkSubsystem* Subsystem = GetWorld()->GetSubsystem<ULandmarkSubsystem>())
    {
        if (!JsonFileName.IsEmpty())
        {
            Subsystem->LoadLandmarksFromFile(JsonFileName);
        }
        else if (Landmarks.Num() > 0)
        {
            // Fallback: If no file specified but we have legacy data in array, register it
            // (Only for quick debug, normally usage uses JSON)
            for (const FLandmarkInstanceData& Data : Landmarks)
            {
                Subsystem->RegisterLandmark(Data);
            }
        }
    }

    // 2. Zero Overhead Optimization
    // We have handed off the instructions (or data). Now we die.
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

void ULandmarkCloudComponent::LoadFromJson()
{
    // Reuse Subsystem logic if possible? No, Subsystem is Runtime.
    // We are likely in Editor. Can we access Subsystem? Yes, Editor World has Subsystems.
    // But Subsystem loads into "RegisteredLandmarks", we want to load into "Landmarks" TArray.
    // So we need explicit logic here.
    
    FString RelativePath = FPaths::ProjectContentDir() / TEXT("MapData") / JsonFileName;
    FString JsonString;
    
    if (FFileHelper::LoadFileToString(JsonString, *RelativePath))
    {
         if (FJsonObjectConverter::JsonArrayStringToUStruct(JsonString, &Landmarks, 0, 0))
         {
             UE_LOG(LogTemp, Log, TEXT("LandmarkCloud: Loaded %d points from %s"), Landmarks.Num(), *RelativePath);
         }
         else
         {
             UE_LOG(LogTemp, Error, TEXT("LandmarkCloud: Failed to parse JSON."));
         }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("LandmarkCloud: File not found %s"), *RelativePath);
    }
}

void ULandmarkCloudComponent::SaveToJson()
{
    FString RelativePath = FPaths::ProjectContentDir() / TEXT("MapData") / JsonFileName;
    FString JsonString;
    
    if (FJsonObjectConverter::UStructArrayToJsonString(Landmarks, JsonString))
    {
        if (FFileHelper::SaveStringToFile(JsonString, *RelativePath))
        {
            UE_LOG(LogTemp, Log, TEXT("LandmarkCloud: Saved %d points to %s"), Landmarks.Num(), *RelativePath);
        }
    }
}
