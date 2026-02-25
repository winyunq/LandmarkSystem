#include "LandmarkSettings.h"

ULandmarkSettings::ULandmarkSettings()
{
    CityLabelZOffset = 147.0f;

    static const FString DefaultConfigPath = TEXT("/Game/Unit/Actor/Building/City/Gen_SK_Flag_A/AgentConfig_SK_Flag_A.AgentConfig_SK_Flag_A");

    auto MakeEntry = [&](const FString& Type) -> FCityLevelConfig
    {
        FCityLevelConfig Cfg;
        Cfg.TypeName = Type;
        Cfg.MassConfig = TSoftObjectPtr<UMassBattleAgentConfigDataAsset>(FSoftObjectPath(DefaultConfigPath));
        return Cfg;
    };

    // City1 ~ City5，共 5 个等级
    CityLevelConfigs.Empty();
    CityLevelConfigs.Add(MakeEntry(TEXT("City1")));
    CityLevelConfigs.Add(MakeEntry(TEXT("City2")));
    CityLevelConfigs.Add(MakeEntry(TEXT("City3")));
    CityLevelConfigs.Add(MakeEntry(TEXT("City4")));
    CityLevelConfigs.Add(MakeEntry(TEXT("City5")));
}

const ULandmarkSettings* ULandmarkSettings::Get()
{
    return GetDefault<ULandmarkSettings>();
}

const FCityLevelConfig* ULandmarkSettings::FindCityConfig(const FString& TypeName) const
{
    for (const FCityLevelConfig& Cfg : CityLevelConfigs)
    {
        if (Cfg.TypeName.Equals(TypeName, ESearchCase::IgnoreCase))
        {
            return &Cfg;
        }
    }
    return nullptr;
}
