#include "LandmarkSettings.h"

ULandmarkSettings::ULandmarkSettings()
{
    CityLabelZOffset = 64.0f;
    BaseFontScale = 1.0f;
    bForceNeutralCityOwnership = true;

    FLandmarkMapProfile EastAsia;
    EastAsia.MapPackagePath = TEXT("/Game/Map/EastAsia/64");
    EastAsia.LandmarkFile = TEXT("Landmarks_EastAsia_64.json");
    EastAsia.FlagSetIndex = 0;

    FLandmarkMapProfile Europe;
    Europe.MapPackagePath = TEXT("/Game/Map/Europe/64");
    Europe.LandmarkFile = TEXT("Landmarks_Europe_64.json");
    Europe.FlagSetIndex = 1;

    MapProfiles = { MoveTemp(EastAsia), MoveTemp(Europe) };

    static const FString DefaultConfigPath = TEXT("/Game/Unit/Actor/Building/City/Common_City_Settlement/Common_City_Settlement.Common_City_Settlement");

    auto MakeEntry = [&](const FString& Type, float GDPPerFactory, int32 VisualLevel) -> FCityLevelConfig
    {
        FCityLevelConfig Cfg;
        Cfg.TypeName = Type;
        Cfg.MassConfig = TSoftObjectPtr<UMassBattleAgentConfigDataAsset>(FSoftObjectPath(DefaultConfigPath));
        Cfg.VisualLevel = VisualLevel;
        Cfg.FactoryBuildCost = 365.0f;
        Cfg.GDPPerFactoryPerSettlement = GDPPerFactory;
        return Cfg;
    };

    // City tier controls intrinsic factory count through Victory Points. Factory
    // output itself is terrain-driven by the authoritative GameState economy.
    CityLevelConfigs.Empty();
    CityLevelConfigs.Add(MakeEntry(TEXT("City1"), 7.0f, 5));
    CityLevelConfigs.Add(MakeEntry(TEXT("City2"), 7.0f, 4));
    CityLevelConfigs.Add(MakeEntry(TEXT("City3"), 7.0f, 3));
    CityLevelConfigs.Add(MakeEntry(TEXT("City4"), 7.0f, 2));
    CityLevelConfigs.Add(MakeEntry(TEXT("City5"), 7.0f, 1));
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

const FLandmarkMapProfile* ULandmarkSettings::FindMapProfile(
    const FString& MapPackagePath) const
{
    return MapProfiles.FindByPredicate(
        [&MapPackagePath](const FLandmarkMapProfile& Profile)
        {
            return !Profile.MapPackagePath.IsNone()
                && MapPackagePath.Equals(
                    Profile.MapPackagePath.ToString(), ESearchCase::IgnoreCase);
        });
}
