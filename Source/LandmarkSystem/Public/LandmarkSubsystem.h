#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LandmarkTypes.h"
#include "MassAPIStructs.h"
#include "LandmarkSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLandmarkSystem, Log, All);

/**
 * ULandmarkSubsystem
 * 
 * 地标系统核心子系统。
 * - 从 JSON 加载地标数据 (Landmark = HUD 层)
 * - 基于配置批量生成城市 Mass 实体 (City = 渲染层)
 * - 两者通过 XY 坐标 + FLandmarkFragment 铆钉
 */
UCLASS()
class LANDMARKSYSTEM_API ULandmarkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// --- Registration API ---
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void RegisterLandmark(const FLandmarkInstanceData& Data);

	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UpdateLandmark(const FString& ID, const FLandmarkInstanceData& NewData);

	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UnregisterLandmark(const FString& ID);

	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UnregisterAll();

	// --- File I/O ---
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	bool LoadLandmarksFromFile(const FString& FileName);

	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	bool SaveLandmarksToFile(const FString& FileName, const TArray<FLandmarkInstanceData>& DataToSave);

	// --- Runtime API ---
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void UpdateCameraState(const FVector& CameraLocation, const FRotator& CameraRotation, float FOV, float ZoomFactor);

	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void DrawLandmarks(class UCanvas* InCanvas);

	void GetVisibleLandmarks(TArray<FLandmarkInstanceData>& OutVisibleLandmarks, TArray<FVector2D>& OutScreenPositions, TArray<float>& OutScales, TArray<float>& OutAlphas);

	// --- Command Grid Mapping ---
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem")
	void RegisterTypeGrid(const FString& Type, class URTSCommandGridAsset* GridAsset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "LandmarkSystem")
	class URTSCommandGridAsset* GetGridByType(const FString& Type) const;

	/** 根据 Mass 实体句柄反向查询城市类型 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "LandmarkSystem")
	FString FindTypeByEntity(FEntityHandle Handle) const;

	// --- Configuration ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandmarkSystem")
	FRuntimeFloatCurve ScaleCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandmarkSystem")
	FRuntimeFloatCurve AlphaCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandmarkSystem")
	TObjectPtr<UFont> NameFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandmarkSystem")
	TObjectPtr<UFont> VPFont;

protected:
	UPROPERTY()
	TMap<FString, FLandmarkInstanceData> RegisteredLandmarks;

	TArray<FString> VisibleLandmarkIDs;
	TArray<FVector2D> CachedScreenPositions;
	TArray<float> CachedScales;
	TArray<float> CachedAlphas;

	UPROPERTY()
	TMap<FString, TObjectPtr<class URTSCommandGridAsset>> TypeGridAssets;

	TMap<FIntPoint, TArray<FString>> SpatialGrid;

	float SpatialCellSize = 10000.0f;
	void RebuildSpatialGrid();

private:
	/** 批量生成所有城市类型的 Mass 实体，通过 ULandmarkSettings 读取配置 */
	void BatchSpawnAllCities();

	/** 按类型名批量生成一组城市实体，返回句柄数组 */
	TArray<FEntityHandle> BatchSpawnCityType(const FString& TypeName, const TArray<FVector>& Locations, int32 Team = 0);

	FVector LastCameraLoc;
	FRotator LastCameraRot;
	float LastZoomFactor = 0.5f;

	bool ProjectWorldLocationToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const;
};
