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
	
	// Default Curve Setup:
	// Zoom 0.0 (Close): Scale 0.5, Alpha 0.0 (Hide)
	// Zoom 0.5 (Mid): Scale 1.0, Alpha 1.0
	// Zoom 1.0 (Far): Scale 2.0, Alpha 1.0
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
    FString JsonString;
    
    if (FJsonObjectConverter::UStructArrayToJsonString(DataToSave, JsonString))
    {
        if (FFileHelper::SaveStringToFile(JsonString, *RelativePath))
        {
            UE_LOG(LogTemp, Log, TEXT("LandmarkSubsystem: Saved %d landmarks to %s"), DataToSave.Num(), *RelativePath);
            return true;
        }
    }
    
    UE_LOG(LogTemp, Error, TEXT("LandmarkSubsystem: Failed to save JSON to %s"), *RelativePath);
    return false;
}

void ULandmarkSubsystem::UpdateCameraState(const FVector& CameraLocation, const FRotator& CameraRotation, float FOV, float ZoomFactor)
{
	LastCameraLoc = CameraLocation;
	LastCameraRot = CameraRotation;
	LastZoomFactor = FMath::Clamp(ZoomFactor, 0.0f, 1.0f);

	VisibleLandmarkIDs.Reset();
	CachedScreenPositions.Reset();
	CachedScales.Reset();
	CachedAlphas.Reset();

	// Adaptive Scaling Logic
	// Logic: Use curves to determine global scale multiplier based on zoom
	// TODO: Use the curves properly if they have data, otherwise fallback
	float GlobalScaleMult = FMath::Lerp(0.5f, 2.0f, ZoomFactor); // Fallback Linear
	float GlobalAlpha = FMath::Lerp(0.0f, 1.0f, ZoomFactor * 5.0f); // Fast fade in at start
	GlobalAlpha = FMath::Clamp(GlobalAlpha, 0.0f, 1.0f);

	if (const FRichCurve* ScaleKeys = ScaleCurve.GetRichCurveConst())
	{
		if (ScaleKeys->GetNumKeys() > 0)
		{
			GlobalScaleMult = ScaleKeys->Eval(ZoomFactor);
		}
	}
	if (const FRichCurve* AlphaKeys = AlphaCurve.GetRichCurveConst())
	{
		if (AlphaKeys->GetNumKeys() > 0)
		{
			GlobalAlpha = AlphaKeys->Eval(ZoomFactor);
		}
	}

	// Frustum Culling & Projection
	// Note: Standard ProjectWorldLocationToScreen requires a PlayerController. 
	// For pure math without PC dependency, we'd need ViewProjectionMatrix.
	// But usually Subsystems run in context where PC is available.
	
	for (const auto& Pair : RegisteredLandmarks)
	{
		const FLandmarkInstanceData& Data = Pair.Value;

		// 1. Zoom Level Filtering
		if (ZoomFactor < Data.VisualConfig.MinVisibleZoom || ZoomFactor > Data.VisualConfig.MaxVisibleZoom)
		{
			continue;
		}

		// 2. Location
		FVector Location = Data.WorldLocation;
		if (Data.LinkedActor.IsValid())
		{
			Location = Data.LinkedActor->GetActorLocation();
		}

		// 3. Projection
		FVector2D ScreenPos;
		bool bOnScreen = ProjectWorldLocationToScreen(Location, ScreenPos);

		if (bOnScreen)
		{
			VisibleLandmarkIDs.Add(Data.ID);
			CachedScreenPositions.Add(ScreenPos);
			
			// Per-landmark scale * Global Adaptive scale
			float FinalScale = Data.VisualConfig.BaseScale * GlobalScaleMult;
			CachedScales.Add(FinalScale);
			
			CachedAlphas.Add(GlobalAlpha);
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

bool ULandmarkSubsystem::ProjectWorldLocationToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const
{
	// Simple wrapper around UGameplayStatics
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		return PC->ProjectWorldLocationToScreen(WorldLocation, OutScreenPosition, true);
	}
	return false;
}
