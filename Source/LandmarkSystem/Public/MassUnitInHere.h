// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassEntityTypes.h"
#include "MassUnitInHere.generated.h"

class UMassBattleAgentConfigDataAsset;
class UStaticMeshComponent;

/**
 * Editor placement actor for spawning a local group of Mass units.
 * 一个点大量单位：适合在关卡中手工摆放初始部队、守军、建筑群等。
 */
UCLASS()
class LANDMARKSYSTEM_API AMassUnitInHere : public AActor
{
	GENERATED_BODY()

public:
	AMassUnitInHere();

	/**
	 * Builds every enabled UnitHere actor in stable actor-name order during the
	 * shared Tick-0 level-initialization phase. Returns completed formations.
	 */
	static int32 InitializeAllLevelUnits(UWorld& World);

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	/** 是否启用这个场景代理。关闭时 BeginPlay 不生成单位，运行时直接移除自身，等同于临时注释掉。 */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here")
	bool bSpawnEnabled = true;

	/** Mass 单位配置 */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here")
	TObjectPtr<UMassBattleAgentConfigDataAsset> AgentConfig;

	/** 生成数量 */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here", meta = (ClampMin = "1", UIMin = "1"))
	int32 Quantity = 16;

	/** 阵营 ID */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here", meta = (ClampMin = "0", ClampMax = "9"))
	int32 Team = 0;

	/** 生命值重载，0 表示使用单位配置默认值 */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HealthOverride = 0.f;

	/** 单位生成间距。数量很大时生成区域按 sqrt(Quantity) 展开，避免过密导致移动/避障卡死。 */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SpawnSpacing = 150.0f;

	/** 大编队使用 MassBattleFrame 原生 deferred spawn 分帧创建，避免 BeginPlay 长时间锁死主线程。 */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here|Performance")
	bool bDeferLargeSpawns = true;

	/** 每个生成步骤的目标单位数；实际总步数由 Quantity 自动计算。 */
	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here|Performance", meta = (ClampMin = "1", UIMin = "1"))
	int32 AgentsPerSpawnStep = 250;

	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here")
	bool bOverrideHealthBarVisibility = false;

	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here", meta = (EditCondition = "bOverrideHealthBarVisibility"))
	bool bShowHealthBarByDefault = false;

	UPROPERTY(EditAnywhere, Category = "Mass Unit In Here", meta = (EditCondition = "bOverrideHealthBarVisibility"))
	bool bShowHealthBarOnSelected = true;

private:
	/** 编辑器内的预览网格组件 */
	UPROPERTY(VisibleAnywhere, Category = "Mass Unit In Here")
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	void UpdatePreview();
	bool IsConfiguredCityUnit() const;
	int32 ResolveMapFlagSetIndex() const;
	bool InitializeLevelUnits(bool bForceSynchronous);
	void ApplySpawnOverrides(const TArray<FEntityHandle>& SpawnedEntities);
	bool bLevelUnitsInitialized = false;

	UFUNCTION()
	void HandleDeferredSpawnFinished(const TArray<FEntityHandle>& SpawnedEntities);
};
