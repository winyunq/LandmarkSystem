#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LandmarkSettings.generated.h"

class UMassBattleAgentConfigDataAsset;
class URTSCommandGridAsset;

/**
 * 每个城市等级的配置条目（City1 ~ City5）
 */
USTRUCT(BlueprintType)
struct FCityLevelConfig
{
	GENERATED_BODY()

	/** 城市等级标识，与 JSON 中的 Type 字段匹配 (e.g. "City1") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FString TypeName;

	/** 该等级使用的 Mass Agent 配置资产（僵尸模型占位） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TSoftObjectPtr<UMassBattleAgentConfigDataAsset> MassConfig;

	/** 该等级对应的 RTS 指令面板资产 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TSoftObjectPtr<URTSCommandGridAsset> CommandGrid;

	FCityLevelConfig() {}
};

/**
 * ULandmarkSettings
 * 地标系统全局配置，暴露于"项目设置 -> 插件 -> Landmark System"
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Landmark System"))
class LANDMARKSYSTEM_API ULandmarkSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULandmarkSettings();

	static const ULandmarkSettings* Get();

	/** 城市 HUD 标签统一 Z 偏移（相对于城市 XY 坐标，标签显示在地标上方多少单位） */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "City Configs")
	float CityLabelZOffset = 1500.0f;

	/** City1~City5 各等级的配置，按等级顺序排列 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "City Configs",
		meta = (TitleProperty = "TypeName"))
	TArray<FCityLevelConfig> CityLevelConfigs;

	/** 找到特定类型的配置（不区分大小写） */
	const FCityLevelConfig* FindCityConfig(const FString& TypeName) const;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
