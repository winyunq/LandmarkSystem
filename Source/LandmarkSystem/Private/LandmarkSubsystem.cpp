#include "LandmarkSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

void ULandmarkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	// Default Curve Setup:
	// Zoom 0.0 (Close): Scale 0.5, Alpha 0.0 (Hide)
	// Zoom 0.5 (Mid): Scale 1.0, Alpha 1.0
	// Zoom 1.0 (Far): Scale 2.0, Alpha 1.0
	
	// Note: In a real scenario, these keys should be set in a DataAsset or Config
	// Here we just ensure it's not empty for safety
	/*
	FRichCurve* ScaleKeys = ScaleCurve.GetRichCurve();
	ScaleKeys->AddKey(0.0f, 0.5f);
	ScaleKeys->AddKey(0.2f, 0.8f);
	ScaleKeys->AddKey(1.0f, 2.5f);

	FRichCurve* AlphaKeys = AlphaCurve.GetRichCurve();
	AlphaKeys->AddKey(0.0f, 0.0f);
	AlphaKeys->AddKey(0.1f, 1.0f);
	AlphaKeys->AddKey(1.0f, 1.0f);
	*/
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
