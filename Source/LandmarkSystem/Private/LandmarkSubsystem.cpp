#include "LandmarkSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void ULandmarkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void ULandmarkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    // Auto-Load Data for this Map
    
    // Construct filename: "Landmarks_MapName.json"
    // GetMapName() returns "IED_MapName" or just "MapName". Be careful with PIE prefixes.
    FString MapName = InWorld.GetName();
    
    // Remove PIE prefix if needed (UEDPIE_0_...)
    MapName.RemoveFromStart(InWorld.StreamingLevelsPrefix); 
    
    // For simplicity, let's look for "Landmarks_<MapName>.json"
    FString FileName = FString::Printf(TEXT("Landmarks_%s.json"), *MapName);
    
    // Try load (Log warning handled by Load function)
    LoadLandmarksFromFile(FileName);
}

void ULandmarkSubsystem::Deinitialize()
{
	RegisteredLandmarks.Empty();
	Super::Deinitialize();
}

void ULandmarkSubsystem::RegisterLandmark(const FLandmarkInstanceData& Data)
{
	FString SafeID = Data.ID;
	if (SafeID.IsEmpty())
	{
		SafeID = FGuid::NewGuid().ToString();
	}
	
	FLandmarkInstanceData NewData = Data;
	NewData.ID = SafeID;

	RegisteredLandmarks.Add(SafeID, NewData);
}

void ULandmarkSubsystem::UpdateLandmark(const FString& ID, const FLandmarkInstanceData& NewData)
{
	if (RegisteredLandmarks.Contains(ID))
	{
		RegisteredLandmarks[ID] = NewData;
	}
}

void ULandmarkSubsystem::UnregisterLandmark(const FString& ID)
{
	RegisteredLandmarks.Remove(ID);
}

void ULandmarkSubsystem::UnregisterAll()
{
	RegisteredLandmarks.Empty();
}

bool ULandmarkSubsystem::LoadLandmarksFromFile(const FString& FileName)
{
    FString RelativePath = FPaths::ProjectContentDir() / TEXT("MapData") / FileName;
    FString JsonString;
    
    if (!FFileHelper::LoadFileToString(JsonString, *RelativePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("LandmarkSubsystem: Failed to load file %s"), *RelativePath);
        return false;
    }

    TArray<FLandmarkInstanceData> ImportData;
    if (FJsonObjectConverter::JsonArrayStringToUStruct(JsonString, &ImportData, 0, 0))
    {
        for (const FLandmarkInstanceData& Data : ImportData)
        {
            RegisterLandmark(Data);
        }
        UE_LOG(LogTemp, Log, TEXT("LandmarkSubsystem: Successfully loaded %d landmarks from %s"), ImportData.Num(), *FileName);
        return true;
    }
    
    UE_LOG(LogTemp, Error, TEXT("LandmarkSubsystem: Failed to parse JSON from %s"), *FileName);
    return false;
}

bool ULandmarkSubsystem::SaveLandmarksToFile(const FString& FileName, const TArray<FLandmarkInstanceData>& DataToSave)
{
    FString RelativePath = FPaths::ProjectContentDir() / TEXT("MapData") / FileName;
    
    TArray<TSharedPtr<FJsonValue>> JsonArray;
    
    // Manual serialization because UStructArrayToJson might not exist or isn't exposed correctly
    for (const FLandmarkInstanceData& Data : DataToSave)
    {
        TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
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
                UE_LOG(LogTemp, Log, TEXT("LandmarkSubsystem: Saved %d landmarks to %s"), DataToSave.Num(), *RelativePath);
                return true;
            }
        }
    }
    
    UE_LOG(LogTemp, Error, TEXT("LandmarkSubsystem: Failed to save JSON to %s"), *RelativePath);
    return false;
}

void ULandmarkSubsystem::UpdateCameraState(const FVector& CameraLocation, const FRotator& CameraRotation, float FOV, float ZoomFactor)
{
	LastCameraLoc = CameraLocation;
	LastCameraRot = CameraRotation;
    // ZoomFactor input is now less relevant for logic, but maybe useful for something else.
    // We primarily use CameraLocation.Z for "Level" logic.

	VisibleLandmarkIDs.Reset();
	CachedScreenPositions.Reset();
	CachedScales.Reset();
	CachedAlphas.Reset();

    // Standard Height-based filtering
    float CameraHeight = CameraLocation.Z;

	// Optimization: 
    // Current implementation is O(N). For 65k map size, a QuadTree would be better.
    // But for <2000 landmarks, O(N) is negligible in C++.
    // 1. Frustum Culling (Project)
    // 2. Height Filtering
	
	for (const auto& Pair : RegisteredLandmarks)
	{
		const FLandmarkInstanceData& Data = Pair.Value;

		// 1. Height Level Filtering
        // "Level 1" = High Altitude, "Level 2" = Low Altitude
		if (CameraHeight < Data.VisualConfig.MinVisibleHeight || CameraHeight > Data.VisualConfig.MaxVisibleHeight)
		{
			continue;
		}

		// 2. Location
		FVector Location = Data.WorldLocation;
		if (Data.LinkedActor.IsValid())
		{
			Location = Data.LinkedActor->GetActorLocation();
		}

		// 3. Projection (Frustum Check)
		FVector2D ScreenPos;
		bool bOnScreen = ProjectWorldLocationToScreen(Location, ScreenPos);

		if (bOnScreen)
		{
			VisibleLandmarkIDs.Add(Data.ID);
			CachedScreenPositions.Add(ScreenPos);
			
			// Constant Scale as requested
			CachedScales.Add(Data.VisualConfig.BaseScale);
			
            // Constant Alpha for now, or fade out at edges? Keep simple.
			CachedAlphas.Add(1.0f);
		}
	}
}

void ULandmarkSubsystem::GetVisibleLandmarks(TArray<FLandmarkInstanceData>& OutVisibleLandmarks, TArray<FVector2D>& OutScreenPositions, TArray<float>& OutScales, TArray<float>& OutAlphas)
{
	OutVisibleLandmarks.Reset();
	OutScreenPositions = CachedScreenPositions;
	OutScales = CachedScales;
	OutAlphas = CachedAlphas;

	for (const FString& ID : VisibleLandmarkIDs)
	{
		if (FLandmarkInstanceData* Ptr = RegisteredLandmarks.Find(ID))
		{
			OutVisibleLandmarks.Add(*Ptr);
		}
		else
		{
			// Should not happen, but keep arrays synced
			OutVisibleLandmarks.Add(FLandmarkInstanceData()); 
		}
	}
}

void ULandmarkSubsystem::DrawLandmarks(UCanvas* InCanvas)
{
    if (!InCanvas) return;

    // Use cached data directly
    for (int32 i = 0; i < VisibleLandmarkIDs.Num(); ++i)
    {
        if (!CachedScreenPositions.IsValidIndex(i)) continue;

        FString ID = VisibleLandmarkIDs[i];
        FLandmarkInstanceData* DataPtr = RegisteredLandmarks.Find(ID);
        if (!DataPtr) continue;

        const FLandmarkInstanceData& Data = *DataPtr;
        const FVector2D& ScreenPos = CachedScreenPositions[i];
        float Scale = CachedScales[i];
        float Alpha = CachedAlphas[i];
        
        if (Alpha <= 0.01f) continue;

        // Draw Text
        FCanvasTextItem TextItem(
            ScreenPos,
            Data.DisplayName,
            GEngine->GetLargeFont(), 
            FLinearColor(1.0f, 1.0f, 1.0f, Alpha)
        );
        
        TextItem.Scale = FVector2D(Scale, Scale);
        TextItem.EnableShadow(FLinearColor::Black);
        
        // Measure text for centering
        float XL, YL;
        InCanvas->StrLen(GEngine->GetLargeFont(), Data.DisplayName.ToString(), XL, YL);
        TextItem.DrawnSize = FVector2D(XL, YL);
        
        // Center alignment
        TextItem.Position -= (TextItem.DrawnSize * Scale * 0.5f);

        InCanvas->DrawItem(TextItem);
    }
}

bool ULandmarkSubsystem::ProjectWorldLocationToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const
{
	// Simple wrapper around UGameplayStatics
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		return PC->ProjectWorldLocationToScreen(WorldLocation, OutScreenPosition, true);
	}
	return false;
}
