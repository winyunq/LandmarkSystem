// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
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
};
