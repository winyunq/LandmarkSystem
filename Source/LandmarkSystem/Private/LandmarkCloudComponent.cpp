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
    // No Runtime Logic. This component is an Editor-Only tool.
    // The Subsystem auto-loads landmarks based on map name.
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
    
    // Manual serialization relying on Subsystem or duplicating logic? 
    // Let's duplicate or make static helper. Given compilation error, just fix locally.
    
    TArray<TSharedPtr<FJsonValue>> JsonArray;
    
    for (const FLandmarkInstanceData& Data : Landmarks)
    {
        TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
        if (FJsonObjectConverter::UStructToJsonObject(FLandmarkInstanceData::StaticStruct(), &Data, JsonObj, 0, 0))
        {
            JsonArray.Add(MakeShared<FJsonValueObject>(JsonObj));
        }
    }

    if (JsonArray.Num() > 0)
    {
        FString JsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
        if (FJsonSerializer::Serialize(JsonArray, Writer))
        {
            if (FFileHelper::SaveStringToFile(JsonString, *RelativePath))
            {
                UE_LOG(LogTemp, Log, TEXT("LandmarkCloud: Saved %d points to %s"), Landmarks.Num(), *RelativePath);
            }
        }
    }
}
