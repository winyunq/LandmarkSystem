#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LandmarkSettings.generated.h"

class UMassBattleAgentConfigDataAsset;
class URTSCommandGridAsset;

/** One exact world-package to landmark-data binding. */
USTRUCT(BlueprintType)
struct FLandmarkMapProfile
{
	GENERATED_BODY()

	/** Canonical long package name, for example /Game/Map/Europe/64. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FName MapPackagePath = NAME_None;

	/** JSON file relative to Content/MapData. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString LandmarkFile;

	/** Row/set in the shared theater flag atlas. Bound by exact map package path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 FlagSetIndex = 0;
};

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

	/**
	 * 同一套 Native ISKM City 网格的视觉等级。不会改变 256uu 占地；
	 * 只通过 PerInstanceCustomData 显示对应的外城、中城和内城建筑层级。
	 * 当前项目语义为 City1=最大城市（5），City5=最小城市（1）。
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "City|Visual",
		meta = (ClampMin = "1", ClampMax = "5", UIMin = "1", UIMax = "5"))
	int32 VisualLevel = 1;

	/** 建造一座工厂需要的现金。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Economy", meta = (ClampMin = "0"))
	float FactoryBuildCost = 365.0f;

	/** 每座工厂在一次（默认 7 天）结算中提供的 GDP。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Economy", meta = (ClampMin = "0"))
	float GDPPerFactoryPerSettlement = 3.0f;

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

	/** 城市 HUD 标签相对地面点的 Z 偏移。 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "City Configs")
	float CityLabelZOffset = 64.0f;

	/** 标签基础缩放比例，避免代码中硬编码魔数 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "City Configs")
	float BaseFontScale = 1.0f;

	/**
	 * Exact package-path bindings. The short asset name is never used because
	 * multiple theaters may legitimately contain a level named "64". Maps with
	 * no exact binding intentionally load no file-backed landmarks.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Map Profiles",
		meta = (TitleProperty = "MapPackagePath"))
	TArray<FLandmarkMapProfile> MapProfiles;

	/**
	 * Optional unified city-name table relative to Content/MapData. The table
	 * format is the localization_table/CityNames_AllLanguages.json file from the
	 * multilingual city package. Leave empty to use only embedded names or
	 * culture-specific map files such as Content/MapData/zh-Hans/<LandmarkFile>.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Localization")
	FString CityNameLocalizationTableFile =
		TEXT("localization_table/CityNames_AllLanguages.json");

	/** 战役开局强制所有城市归属 Team 0（中立/野怪）。 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "City Configs")
	bool bForceNeutralCityOwnership = true;

	/** City1~City5 各等级的配置，按等级顺序排列 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "City Configs",
		meta = (TitleProperty = "TypeName"))
	TArray<FCityLevelConfig> CityLevelConfigs;

	/** 找到特定类型的配置（不区分大小写） */
	const FCityLevelConfig* FindCityConfig(const FString& TypeName) const;

	/** Exact, case-insensitive canonical long-package lookup. */
	const FLandmarkMapProfile* FindMapProfile(const FString& MapPackagePath) const;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
