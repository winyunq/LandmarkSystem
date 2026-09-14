#pragma once

#include "CoreMinimal.h"
#include "CanvasItem.h"
#include "Curves/CurveFloat.h"
#include "Subsystems/WorldSubsystem.h"
#include "LandmarkTypes.h"
#include "MassAPIStructs.h"
#include "LandmarkSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLandmarkSystem, Log, All);
DECLARE_MULTICAST_DELEGATE_FourParams(
	FOnLandmarkTeamChangeRequestedNative,
	const FLandmarkInstanceData&,
	int32,
	int32,
	bool&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnLandmarkTeamChangedNative, const FLandmarkInstanceData&, int32, int32);
DECLARE_MULTICAST_DELEGATE(FOnLandmarksInitializedNative);

struct FRTSSelectionView;
struct FRTSUnitData;
class ALandscapeProxy;
class UFont;
class UStaticMesh;

/**
 * ULandmarkSubsystem
 * 
 * 地标系统核心子系统。
 * - 从 JSON 加载地标数据 (Landmark = HUD 层)
 * - 基于配置批量生成城市 Mass 实体 (City = 渲染层)
 * - 两者通过 XY 坐标 + FLandmarkFragment 铆钉
 */
UCLASS()
class LANDMARKSYSTEM_API ULandmarkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	void SerializeWorldSnapshot(FArchive& Ar, TFunctionRef<void(FEntityHandle&)> Entity);

	/** Pre-transfer veto hook. Listeners may set bAllowTransfer to false. */
	FOnLandmarkTeamChangeRequestedNative OnLandmarkTeamChangeRequestedNative;

	/** Native notification used by the game economy when a Mass city changes owner. */
	FOnLandmarkTeamChangedNative OnLandmarkTeamChangedNative;

	/** Fired once all map-authored landmark, city, and level-unit initialization is complete. */
	FOnLandmarksInitializedNative OnLandmarksInitializedNative;

	/** True while/after map-authored entities are ready for deterministic Tick-0 extensions. */
	bool IsMapInitializationReady() const { return bMapInitializationReady; }

	/** Called once by the level-unit initializer while its shared Tick-0 scope is active. */
	void NotifyMapInitializationReady();

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

	/** Resolve one landmark name; an empty culture uses the Name loaded from the selected JSON. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "LandmarkSystem|Localization")
	FString GetLandmarkDisplayName(
		const FString& LandmarkID,
		const FString& CultureName = FString()) const;

	/** Set an optional lookup override; pass empty to use the selected culture JSON again. */
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem|Localization")
	void SetLocalNameCultureOverride(const FString& CultureName);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "LandmarkSystem|Localization")
	FString GetLocalNameCultureOverride() const { return LocalNameCultureOverride; }

	/** Runtime lookup helpers for pure-Mass city gameplay (no Actor per city required). */
	const FLandmarkInstanceData* FindLandmarkByEntity(FEntityHandle Handle) const;
	FLandmarkInstanceData* FindMutableLandmarkByEntity(FEntityHandle Handle);
	const FLandmarkInstanceData* FindLandmarkByID(const FString& ID) const;
	FLandmarkInstanceData* FindMutableLandmarkByID(const FString& ID);
	const TMap<FString, FLandmarkInstanceData>& GetRegisteredLandmarks() const { return RegisteredLandmarks; }
	uint32 GetLandmarkRevision() const { return LandmarkRevision; }
	void SetCapitalCity(const FString& LandmarkID, int32 TeamIndex);

	/**
	 * Resolve an XY point against the map's Landscape heightfield directly.
	 * This deliberately bypasses world collision so decorative water planes and
	 * other WorldStatic actors cannot become city or unit ground.
	 */
	bool ResolveLandscapeGroundLocation(
		const FVector& InLocation,
		FVector& OutGroundLocation) const;
	bool IsLandscapeGroundActor(const AActor* Actor) const;

	/** Change a live Mass city's owner while keeping its fragment, tags and HUD data in sync. */
	UFUNCTION(BlueprintCallable, Category = "LandmarkSystem|Ownership")
	bool TransferLandmarkTeam(const FString& LandmarkID, int32 NewTeamIndex);

	/** Called only by the city FDyingTag observer; restores and transfers one city. */
	bool CaptureCityFromLethalDamage(
		FEntityHandle CityEntity,
		int32 CapturingTeamIndex);

	UFUNCTION(BlueprintPure, Category = "LandmarkSystem|Economy")
	float GetFactoryBuildCost(const FString& Type) const;

	UFUNCTION(BlueprintPure, Category = "LandmarkSystem|Economy")
	float GetGDPPerFactoryPerSettlement(const FString& Type) const;

	/** Development-only validation hook used by Landmark.DebugCaptureFirstCity. */
	bool DebugCaptureFirstCity(int32 CapturingTeam);

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
	TArray<TWeakObjectPtr<ALandscapeProxy>> LandscapeGroundSources;

	float SpatialCellSize = 10000.0f;
	void RebuildSpatialGrid();

private:
	// Prepared by registration/data changes; DrawLandmarks only positions and submits these items.
	struct FLandmarkText
	{
		FCanvasTextItem Name{FVector2D::ZeroVector, FText::GetEmpty(), static_cast<UFont*>(nullptr), FLinearColor::White};
		FCanvasTextItem VictoryPoints{FVector2D::ZeroVector, FText::GetEmpty(), static_cast<UFont*>(nullptr), FLinearColor::White};
		FVector2D NameSize = FVector2D::ZeroVector;
		FVector2D VictoryPointsSize = FVector2D::ZeroVector;
	};
	TMap<FString, FLandmarkText> LandmarkText;
	void PrepareLandmarkText(const FString& ID, const FLandmarkInstanceData& Data);
	void RebuildLandmarkText();

	bool bMapInitializationReady = false;
	/** 批量生成所有城市类型的 Mass 实体，通过 ULandmarkSettings 读取配置 */
	void BatchSpawnAllCities();
	void CacheLandscapeGroundSources(UWorld& World);
	void ResolveUnspecifiedLandmarkHeights();

	/** 按类型名批量生成一组城市实体，返回句柄数组 */
	TArray<FEntityHandle> BatchSpawnCityType(const FString& TypeName, const TArray<FVector>& Locations, int32 Team = 0);

	FVector LastCameraLoc;
	FRotator LastCameraRot;
	float LastZoomFactor = 0.5f;
	int32 ActiveFlagSetIndex = 0;
	bool bCityFlagMaterialConfigured = false;
	FString LocalNameCultureOverride;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ActiveCityRuntimeMesh;

	/** Gameplay City -> attached visual flag Agent. The gameplay entity keeps the one-cell base. */
	TMap<FEntityHandle, FEntityHandle> CityFlagEntities;

	/** Previous gameplay health used to mirror real damage onto the visual-only flag Agent. */
	TMap<FEntityHandle, float> CityHealthSamples;

	FDelegateHandle CommandGridResolverHandle;
	FDelegateHandle UnitDataEnricherHandle;
	uint32 LandmarkRevision = 1;

	void BumpLandmarkRevision();
	void ResolveMassCommandGrid(UObject* WorldContextObject, const FString& ActiveKey, const FRTSSelectionView& SelectionView, class URTSCommandGridAsset*& OutGrid);
	void EnrichMassUnitData(UObject* WorldContextObject, const FEntityHandle& Entity, FRTSUnitData& Data);
	void ConfigureCityFlagMaterialIfReady();
	void SyncCityFlagHitAnimations();
	void UpdateCityFlagTeam(const FEntityHandle& CityEntity, int32 NewTeamIndex);
	void TriggerCityFlagUpdateAnimation(const FEntityHandle& Entity);
	void TriggerCityFlagHitAnimation(const FEntityHandle& Entity);
	FString ResolveLandmarkDisplayName(
		const FLandmarkInstanceData& Data,
		const FString& CultureName = FString()) const;
	void ApplyCityNameLocalizationTable(
		const FString& LandmarkFileName,
		TArray<FLandmarkInstanceData>& Landmarks) const;

	bool ProjectWorldLocationToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition) const;
};
