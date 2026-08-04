#include "LandmarkSubsystem.h"
#include "LandmarkSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "DynamicRHI.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "Minimap/MapPackageProfilePaths.h"
#include "Data/RTSCommandGridAsset.h"
#include "Commands/RTSCityCommands.h"
// MassBattle
#include "Subsystems/MassBattleAgentSubsystem.h"
#include "Subsystems/MassBattleNetworkSubsystem.h"
#include "Subsystems/MassBattleSubsystem.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "MassBattleStructs.h"
#include "MassAPISubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassCommonFragments.h"
#include "MassEntityUtils.h"
#include "Fragments/Damage.h"
#include "Fragments/Debuff.h"
#include "Fragments/Death.h"
#include "Fragments/Health.h"
#include "Fragments/HealthBar.h"
#include "Fragments/Hit.h"
#include "Fragments/Appear.h"
#include "Fragments/Animation.h"
#include "Fragments/AgentHostConfig.h"
#include "Fragments/Attack.h"
#include "Fragments/Avoidance.h"
#include "Fragments/Behavior.h"
#include "Fragments/Collider.h"
#include "Fragments/Event.h"
#include "Fragments/Move.h"
#include "Fragments/Network.h"
#include "Fragments/Render.h"
#include "Fragments/Select.h"
#include "Fragments/StyleType.h"
#include "Fragments/Team.h"
#include "Fragments/Transform.h"
#include "Fragments/MassBattleISKMFragment.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "FuncLibs/MassBattleTagHelpers.h"
#include "MassBattleEnums.h"
#include "MassBattleFogVisionSourceFragment.h"
#include "RTSSelectionSubsystem.h"
#include "RTSSelectionStructs.h"
#include "HAL/IConsoleManager.h"
#include "Stats/Stats.h"
#include "Misc/Crc.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"

DEFINE_LOG_CATEGORY(LogLandmarkSystem);

namespace
{
	void AddCultureCandidate(TArray<FString>& Candidates, const FString& Candidate)
	{
		if (!Candidate.IsEmpty())
		{
			Candidates.AddUnique(Candidate);
		}
	}

	TArray<FString> GetLandmarkCultureCandidates(const FString& RequestedCulture)
	{
		FString Culture = RequestedCulture;
		if (Culture.IsEmpty())
		{
			Culture = FInternationalization::Get().GetCurrentCulture()->GetName();
		}
		Culture.ReplaceInline(TEXT("_"), TEXT("-"));

		TArray<FString> Candidates;
		AddCultureCandidate(Candidates, Culture);

		const FString LowerCulture = Culture.ToLower();
		if (LowerCulture.StartsWith(TEXT("zh")))
		{
			const bool bTraditional =
				LowerCulture.Contains(TEXT("hant"))
				|| LowerCulture.Contains(TEXT("-tw"))
				|| LowerCulture.Contains(TEXT("-hk"))
				|| LowerCulture.Contains(TEXT("-mo"));
			AddCultureCandidate(
				Candidates,
				bTraditional ? TEXT("zh-Hant") : TEXT("zh-Hans"));
		}

		FString Language;
		if (Culture.Split(TEXT("-"), &Language, nullptr))
		{
			AddCultureCandidate(Candidates, Language);
		}
		else
		{
			AddCultureCandidate(Candidates, Culture);
		}
		return Candidates;
	}

	const FString* FindCultureName(
		const TMap<FString, FString>& Names,
		const FString& Culture)
	{
		for (const TPair<FString, FString>& Pair : Names)
		{
			if (Pair.Key.Equals(Culture, ESearchCase::IgnoreCase)
				&& !Pair.Value.IsEmpty())
			{
				return &Pair.Value;
			}
		}
		return nullptr;
	}

	bool IsSafeCultureDirectory(const FString& Culture)
	{
		for (const TCHAR Character : Culture)
		{
			if (!FChar::IsAlnum(Character)
				&& Character != TEXT('-')
				&& Character != TEXT('_'))
			{
				return false;
			}
		}
		return !Culture.IsEmpty();
	}

	bool ShouldUseSingleEntityCityTopology()
	{
		FString Topology;
		if (!FParse::Value(FCommandLine::Get(), TEXT("CityTopology="), Topology))
		{
			return true;
		}
		return !Topology.Equals(TEXT("legacy"), ESearchCase::IgnoreCase);
	}

	struct FCityBenchmarkTimingStats
	{
		int32 Count = 0;
		float Average = 0.0f;
		float P50 = 0.0f;
		float P95 = 0.0f;
		float P99 = 0.0f;
		float Maximum = 0.0f;
	};

	float CityBenchmarkPercentile(
		const TArray<float>& SortedValues,
		const float Alpha)
	{
		if (SortedValues.IsEmpty())
		{
			return 0.0f;
		}
		const float Position = FMath::Clamp(Alpha, 0.0f, 1.0f)
			* static_cast<float>(SortedValues.Num() - 1);
		const int32 Lower = FMath::FloorToInt(Position);
		const int32 Upper = FMath::CeilToInt(Position);
		return FMath::Lerp(
			SortedValues[Lower],
			SortedValues[Upper],
			Position - static_cast<float>(Lower));
	}

	FCityBenchmarkTimingStats CalculateCityBenchmarkTimingStats(
		const TArray<float>& Values)
	{
		FCityBenchmarkTimingStats Result;
		if (Values.IsEmpty())
		{
			return Result;
		}

		TArray<float> SortedValues = Values;
		SortedValues.Sort();
		double Sum = 0.0;
		for (const float Value : SortedValues)
		{
			Sum += Value;
		}
		Result.Count = SortedValues.Num();
		Result.Average = static_cast<float>(Sum / SortedValues.Num());
		Result.P50 = CityBenchmarkPercentile(SortedValues, 0.50f);
		Result.P95 = CityBenchmarkPercentile(SortedValues, 0.95f);
		Result.P99 = CityBenchmarkPercentile(SortedValues, 0.99f);
		Result.Maximum = SortedValues.Last();
		return Result;
	}

	TSharedRef<FJsonObject> CityBenchmarkTimingStatsToJson(
		const FCityBenchmarkTimingStats& Stats)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("sample_count"), Stats.Count);
		Json->SetNumberField(TEXT("average"), Stats.Average);
		Json->SetNumberField(TEXT("p50"), Stats.P50);
		Json->SetNumberField(TEXT("p95"), Stats.P95);
		Json->SetNumberField(TEXT("p99"), Stats.P99);
		Json->SetNumberField(TEXT("maximum"), Stats.Maximum);
		return Json;
	}

	struct FCityTopologyBenchmarkState
	{
		TWeakObjectPtr<UWorld> World;
		TArray<float> FrameSamples;
		TArray<float> GameThreadSamples;
		TArray<float> RenderThreadSamples;
		TArray<float> GpuSamples;
		FString OutputDirectory;
		FString MapPackagePath;
		double LastWallClockSeconds = 0.0;
		float PhaseElapsedSeconds = 0.0f;
		float WarmupSeconds = 10.0f;
		float SampleSeconds = 20.0f;
		int32 LogicalCityCount = 0;
		int32 VisualAgentCount = 0;
		bool bEnabled = false;
		bool bSampling = false;
		bool bSingleEntity = false;

		void Begin(
			UWorld& InWorld,
			const FString& InMapPackagePath,
			const int32 InLogicalCityCount,
			const int32 InVisualAgentCount)
		{
			if (!FParse::Param(FCommandLine::Get(), TEXT("CityTopologyBenchmark")))
			{
				return;
			}

			World = &InWorld;
			MapPackagePath = InMapPackagePath;
			LogicalCityCount = InLogicalCityCount;
			VisualAgentCount = InVisualAgentCount;
			bSingleEntity = ShouldUseSingleEntityCityTopology();
			FParse::Value(
				FCommandLine::Get(),
				TEXT("CityBenchmarkWarmup="),
				WarmupSeconds);
			FParse::Value(
				FCommandLine::Get(),
				TEXT("CityBenchmarkSample="),
				SampleSeconds);
			FParse::Value(
				FCommandLine::Get(),
				TEXT("CityBenchmarkOutput="),
				OutputDirectory);
			WarmupSeconds = FMath::Max(0.0f, WarmupSeconds);
			SampleSeconds = FMath::Max(1.0f, SampleSeconds);
			if (OutputDirectory.IsEmpty())
			{
				OutputDirectory = FPaths::ProjectSavedDir()
					/ TEXT("CityTopologyBenchmark");
			}
			OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
			LastWallClockSeconds = FPlatformTime::Seconds();
			PhaseElapsedSeconds = 0.0f;
			bSampling = false;
			bEnabled = true;

			const int32 ReserveCount = FMath::CeilToInt(SampleSeconds * 240.0f);
			FrameSamples.Reserve(ReserveCount);
			GameThreadSamples.Reserve(ReserveCount);
			RenderThreadSamples.Reserve(ReserveCount);
			GpuSamples.Reserve(ReserveCount);
			UE_LOG(LogLandmarkSystem, Display,
				TEXT("CITY_TOPOLOGY_BENCHMARK_READY: topology=%s logical_cities=%d visual_agents=%d expected_city_entities=%d warmup=%.1fs sample=%.1fs map=%s"),
				bSingleEntity ? TEXT("Single") : TEXT("Legacy"),
				LogicalCityCount,
				VisualAgentCount,
				LogicalCityCount * (bSingleEntity ? 1 : 3),
				WarmupSeconds,
				SampleSeconds,
				*MapPackagePath);
		}

		void WriteResult()
		{
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
			const FString ScenarioToken = bSingleEntity
				? TEXT("Single")
				: TEXT("Legacy");
			const FString ResultPath = OutputDirectory / FString::Printf(
				TEXT("%s_%d_cities.json"),
				*ScenarioToken,
				LogicalCityCount);

			const FCityBenchmarkTimingStats FrameStats =
				CalculateCityBenchmarkTimingStats(FrameSamples);
			const FCityBenchmarkTimingStats GameStats =
				CalculateCityBenchmarkTimingStats(GameThreadSamples);
			const FCityBenchmarkTimingStats RenderStats =
				CalculateCityBenchmarkTimingStats(RenderThreadSamples);
			const FCityBenchmarkTimingStats GpuStats =
				CalculateCityBenchmarkTimingStats(GpuSamples);

			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetNumberField(TEXT("benchmark_schema_version"), 1);
			Root->SetStringField(TEXT("scenario"), ScenarioToken);
			Root->SetStringField(TEXT("map"), MapPackagePath);
			Root->SetStringField(
				TEXT("timing_source"),
				TEXT("UE GGameThreadTime + GRenderThreadTime + RHIGetGPUFrameCycles + platform wall frame interval"));
			Root->SetStringField(
				TEXT("render_backend"),
				TEXT("MassBattleISKM; identical cloned AgentConfig in Single scenario"));
			Root->SetBoolField(TEXT("separate_process_per_scenario"), true);
			Root->SetNumberField(TEXT("logical_cities"), LogicalCityCount);
			Root->SetNumberField(TEXT("visual_agents"), VisualAgentCount);
			Root->SetNumberField(
				TEXT("mass_entities_per_city"),
				bSingleEntity ? 1 : 3);
			Root->SetNumberField(
				TEXT("expected_city_related_mass_entities"),
				LogicalCityCount * (bSingleEntity ? 1 : 3));
			Root->SetNumberField(TEXT("warmup_seconds"), WarmupSeconds);
			Root->SetNumberField(TEXT("requested_sample_seconds"), SampleSeconds);
			Root->SetNumberField(
				TEXT("average_fps"),
				FrameStats.Average > 0.0f
					? 1000.0f / FrameStats.Average
					: 0.0f);
			Root->SetBoolField(TEXT("gpu_timing_available"), GpuStats.Count > 0);

			TSharedRef<FJsonObject> Timings = MakeShared<FJsonObject>();
			Timings->SetObjectField(
				TEXT("frame_wall_ms"),
				CityBenchmarkTimingStatsToJson(FrameStats));
			Timings->SetObjectField(
				TEXT("game_thread_ms"),
				CityBenchmarkTimingStatsToJson(GameStats));
			Timings->SetObjectField(
				TEXT("render_thread_ms"),
				CityBenchmarkTimingStatsToJson(RenderStats));
			Timings->SetObjectField(
				TEXT("gpu_frame_ms"),
				CityBenchmarkTimingStatsToJson(GpuStats));
			Root->SetObjectField(TEXT("timings"), Timings);
			Root->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand());
			Root->SetStringField(TEXT("command_line"), FCommandLine::Get());

			FString JsonText;
			const TSharedRef<TJsonWriter<>> Writer =
				TJsonWriterFactory<>::Create(&JsonText);
			FJsonSerializer::Serialize(Root, Writer);
			if (!FFileHelper::SaveStringToFile(JsonText, *ResultPath))
			{
				UE_LOG(LogLandmarkSystem, Error,
					TEXT("CITY_TOPOLOGY_BENCHMARK_FAILED: could not write %s"),
					*ResultPath);
				return;
			}

			UE_LOG(LogLandmarkSystem, Display,
				TEXT("CITY_TOPOLOGY_BENCHMARK_RESULT: topology=%s cities=%d samples=%d frame_avg_ms=%.4f fps=%.2f game_avg_ms=%.4f render_avg_ms=%.4f gpu_avg_ms=%.4f p95_frame_ms=%.4f result=%s"),
				*ScenarioToken,
				LogicalCityCount,
				FrameStats.Count,
				FrameStats.Average,
				FrameStats.Average > 0.0f ? 1000.0f / FrameStats.Average : 0.0f,
				GameStats.Average,
				RenderStats.Average,
				GpuStats.Average,
				FrameStats.P95,
				*ResultPath);
		}

		void Tick(UWorld& InWorld)
		{
			if (!bEnabled || World.Get() != &InWorld)
			{
				return;
			}

			const double CurrentWallClockSeconds = FPlatformTime::Seconds();
			const float FrameWallSeconds = static_cast<float>(FMath::Max(
				CurrentWallClockSeconds - LastWallClockSeconds,
				0.0));
			LastWallClockSeconds = CurrentWallClockSeconds;
			PhaseElapsedSeconds += FrameWallSeconds;

			if (!bSampling)
			{
				if (PhaseElapsedSeconds < WarmupSeconds)
				{
					return;
				}
				bSampling = true;
				PhaseElapsedSeconds = 0.0f;
				FrameSamples.Reset();
				GameThreadSamples.Reset();
				RenderThreadSamples.Reset();
				GpuSamples.Reset();
				UE_LOG(LogLandmarkSystem, Display,
					TEXT("CITY_TOPOLOGY_BENCHMARK_SAMPLE_BEGIN: %s"),
					bSingleEntity ? TEXT("Single") : TEXT("Legacy"));
				return;
			}

			FrameSamples.Add(FrameWallSeconds * 1000.0f);
			GameThreadSamples.Add(static_cast<float>(
				FPlatformTime::ToMilliseconds(GGameThreadTime)));
			RenderThreadSamples.Add(static_cast<float>(
				FPlatformTime::ToMilliseconds(GRenderThreadTime)));
			const float GpuMilliseconds = static_cast<float>(
				FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			if (GpuMilliseconds > 0.0f)
			{
				GpuSamples.Add(GpuMilliseconds);
			}

			if (PhaseElapsedSeconds >= SampleSeconds)
			{
				bEnabled = false;
				WriteResult();
				FPlatformMisc::RequestExit(false);
			}
		}
	};

	FCityTopologyBenchmarkState GCityTopologyBenchmark;
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GDebugCaptureFirstCityCommand(
	TEXT("Landmark.DebugCaptureFirstCity"),
	TEXT("Applies lethal MassBattle damage to the first City using a live unit from the requested team. Usage: Landmark.DebugCaptureFirstCity 1"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 CapturingTeam = Args.IsEmpty() ? 1 : FCString::Atoi(*Args[0]);
		ULandmarkSubsystem* Subsystem = World ? World->GetSubsystem<ULandmarkSubsystem>() : nullptr;
		if (!Subsystem || !Subsystem->DebugCaptureFirstCity(CapturingTeam))
		{
			UE_LOG(LogLandmarkSystem, Error,
				TEXT("Landmark.DebugCaptureFirstCity failed for Team %d. Start a game world with a City and at least one live unit on that team."),
				CapturingTeam);
		}
	}));
#endif

void ULandmarkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CommandGridResolverHandle = URTSSelectionSubsystem::OnResolveMassCommandGrid().AddUObject(
		this, &ULandmarkSubsystem::ResolveMassCommandGrid);
	UnitDataEnricherHandle = URTSSelectionSubsystem::OnEnrichMassUnitData().AddUObject(
		this, &ULandmarkSubsystem::EnrichMassUnitData);
}

// --- VP 默认值辅助函数 ---
static int32 GetDefaultVictoryPoints(const FString& Type)
{
    if (Type.Contains("1")) return 11;
    if (Type.Contains("2")) return 7;
    if (Type.Contains("3")) return 5;
    if (Type.Contains("4")) return 3;
    if (Type.Contains("5")) return 2;
    return 1;
}

void ULandmarkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

    // 1. Load the JSON selected by the canonical full package path. The short
    // map name is intentionally never a key: EastAsia/64 and Europe/64 coexist.
    const FString CanonicalMapPackagePath =
        MassBattleMapProfilePaths::GetCanonicalMapPackagePath(&InWorld);
    UE_LOG(LogLandmarkSystem, Verbose,
        TEXT("LandmarkSubsystem: OnWorldBeginPlay exact map [%s]"),
        *CanonicalMapPackagePath);

    const ULandmarkSettings* Settings = ULandmarkSettings::Get();
    const FLandmarkMapProfile* MapProfile = Settings
        ? Settings->FindMapProfile(CanonicalMapPackagePath)
        : nullptr;
    ActiveFlagSetIndex = MapProfile
        ? FMath::Max(0, MapProfile->FlagSetIndex)
        : 0;
    if (MapProfile)
    {
        const bool bLoadedLandmarks = !MapProfile->LandmarkFile.IsEmpty()
            && LoadLandmarksFromFile(MapProfile->LandmarkFile);
        if (!bLoadedLandmarks)
        {
            // A configured theater must fail closed. Falling through to a
            // different theater's JSON silently corrupts team/city ownership.
            UE_LOG(LogLandmarkSystem, Error,
                TEXT("LandmarkSubsystem: exact map [%s] is bound to missing/invalid [%s]; no cross-map fallback will be loaded."),
                *CanonicalMapPackagePath,
                *MapProfile->LandmarkFile);
        }
        UE_LOG(LogLandmarkSystem, Verbose,
            TEXT("LandmarkSubsystem: map [%s] uses flag set %d."),
            *CanonicalMapPackagePath,
            ActiveFlagSetIndex);
    }
    else
    {
        // An unconfigured map intentionally has no file-backed landmarks.
        // Loading a shared/default JSON here would leak another map's cities.
        UE_LOG(LogLandmarkSystem, Verbose,
            TEXT("LandmarkSubsystem: exact map [%s] has no landmark profile; no landmark file will be loaded."),
            *CanonicalMapPackagePath);
    }

    // 2. 注册城市 Command Grid
    // URTSCityCommandGrid 是 C++ 类而非资产，先创建一个共享实例注册给所有城市类型
    // 如果配置文件里填了资产，则以资产为准（覆盖）
    URTSCityCommandGrid* SharedCityGrid = NewObject<URTSCityCommandGrid>(this);

    if (Settings)
    {
        for (const FCityLevelConfig& Cfg : Settings->CityLevelConfigs)
        {
            // 默认先注册共享 C++ Grid
            RegisterTypeGrid(Cfg.TypeName, SharedCityGrid);

            // 如果项目设置里显式填了资产，用资产覆盖（允许城市等级差异化）
            if (!Cfg.CommandGrid.IsNull())
            {
                if (URTSCommandGridAsset* GridAsset = Cfg.CommandGrid.LoadSynchronous())
                {
                    RegisterTypeGrid(Cfg.TypeName, GridAsset);
                }
            }
        }
        UE_LOG(LogLandmarkSystem, Verbose, TEXT("LandmarkSubsystem: Registered city grid for %d types."), Settings->CityLevelConfigs.Num());
    }
    else
    {
        UE_LOG(LogLandmarkSystem, Warning, TEXT("LandmarkSubsystem: No LandmarkSettings found, city grids not registered."));
    }


    // 3. 批量生成所有城市类型的 Mass 实体（一次性，内存高效）
    BatchSpawnAllCities();
	GCityTopologyBenchmark.Begin(
		InWorld,
		CanonicalMapPackagePath,
		RegisteredLandmarks.Num(),
		CityFlagEntities.Num());
}

void ULandmarkSubsystem::BatchSpawnAllCities()
{
    const ULandmarkSettings* Settings = ULandmarkSettings::Get();
    if (!Settings)
    {
        UE_LOG(LogLandmarkSystem, Error, TEXT("LandmarkSubsystem: ULandmarkSettings not found!"));
        return;
    }

    // 按城市等级和阵营分组收集坐标。
    // 地标 JSON 是“单个单位，大量点”；MassUnitInHere 是“一个点，大量单位”。
    TMap<FString, TMap<int32, TArray<FVector>>> TypeTeamToLocations;
    for (auto& Pair : RegisteredLandmarks)
    {
        FLandmarkInstanceData& Data = Pair.Value;
        if (Data.Value == 0) Data.Value = GetDefaultVictoryPoints(Data.Type);
        // A city intrinsically owns one level-one factory per Victory Point.
        // Preserve any larger persisted total so constructed factories survive reloads.
        Data.FactoryCount = FMath::Max(Data.FactoryCount, Data.Value);
        TypeTeamToLocations.FindOrAdd(Data.Type).FindOrAdd(Data.Team).Add(Data.GetLocation());
    }

    // 每个等级、每个阵营批量生成一次
    for (const FCityLevelConfig& Cfg : Settings->CityLevelConfigs)
    {
        TMap<int32, TArray<FVector>>* TeamToLocations = TypeTeamToLocations.Find(Cfg.TypeName);
        if (!TeamToLocations) continue;

        for (auto& TeamPair : *TeamToLocations)
        {
            const int32 Team = TeamPair.Key;
            TArray<FVector>& Locations = TeamPair.Value;
            if (Locations.Num() == 0) continue;

            TArray<FEntityHandle> Handles = BatchSpawnCityType(Cfg.TypeName, Locations, Team);
            UE_LOG(LogLandmarkSystem, Verbose, TEXT("LandmarkSubsystem: [%s Team %d] Spawned %d/%d entities."),
                *Cfg.TypeName, Team, Handles.Num(), Locations.Num());

            // 将 Handle 写回 RegisteredLandmarks
            int32 HandleIdx = 0;
            for (auto& Pair : RegisteredLandmarks)
            {
                if (HandleIdx >= Handles.Num()) break;
                if (Pair.Value.Type.Equals(Cfg.TypeName, ESearchCase::IgnoreCase) && Pair.Value.Team == Team)
                {
                    Pair.Value.EntityHandle = Handles[HandleIdx++];
                }
            }
        }
    }
}

TArray<FEntityHandle> ULandmarkSubsystem::BatchSpawnCityType(
    const FString& TypeName, const TArray<FVector>& Locations, int32 Team)
{
    if (Locations.Num() == 0) return {};

    const ULandmarkSettings* Settings = ULandmarkSettings::Get();
    if (!Settings) return {};

    const FCityLevelConfig* Cfg = Settings->FindCityConfig(TypeName);
    if (!Cfg || Cfg->MassConfig.IsNull())
    {
        UE_LOG(LogLandmarkSystem, Error, TEXT("LandmarkSubsystem: No MassConfig for [%s]!"), *TypeName);
        return {};
    }

    UMassBattleAgentSubsystem* AgentSub = UMassBattleAgentSubsystem::GetPtr(this);
    if (!AgentSub) return {};

    // 同步加载配置资产（不克隆，仅持有引用）
    UMassBattleAgentConfigDataAsset* DataAsset = Cfg->MassConfig.LoadSynchronous();
    if (!DataAsset)
    {
        UE_LOG(LogLandmarkSystem, Error, TEXT("LandmarkSubsystem: Failed to load MassConfig for [%s]!"), *TypeName);
        return {};
    }

	if (ShouldUseSingleEntityCityTopology())
	{
		static const TCHAR* BenchmarkCityPath =
			TEXT("/Game/MassBattle/City/Benchmark/City_SingleEntity_Benchmark.City_SingleEntity_Benchmark");
		UMassBattleAgentConfigDataAsset* BenchmarkDataAsset =
			LoadObject<UMassBattleAgentConfigDataAsset>(nullptr, BenchmarkCityPath);
		if (!BenchmarkDataAsset)
		{
			UE_LOG(LogLandmarkSystem, Error,
				TEXT("LandmarkSubsystem: single-entity City topology requires AgentConfig [%s]."),
				BenchmarkCityPath);
			return {};
		}

		FEntityTemplateData CityTemplate =
			AgentSub->MakeAgentTemplateDataFromDataAsset(BenchmarkDataAsset);
		FMassEntityTemplateData* CityData = CityTemplate.Get();
		if (!CityData)
		{
			UE_LOG(LogLandmarkSystem, Error,
				TEXT("LandmarkSubsystem: Failed to build single-entity City template for [%s]."),
				*TypeName);
			return {};
		}

		if (FStyleType* Style = CityData->GetMutableFragment<FStyleType>())
		{
			Style->Index = FMath::Clamp(Cfg->VisualLevel, 1, 5);
			Style->Variant = ActiveFlagSetIndex;
		}
		if (FDeath* Death = CityData->GetMutableFragment<FDeath>())
		{
			Death->bEnable = false;
		}
		if (FVisualize* Visualize = CityData->GetMutableFragment<FVisualize>())
		{
			Visualize->bEnable = true;
			Visualize->Transform.SetLocation(FVector3f::ZeroVector);
			Visualize->Transform.SetRotation(FQuat4f::Identity);
			Visualize->Transform.SetScale3D(FVector3f::OneVector);
		}
		if (FMassBattleISKMAddonFragment* ISKM =
			CityData->GetMutableFragment<FMassBattleISKMAddonFragment>())
		{
			ISKM->bEnable = true;
		}

		if (UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this))
		{
			if (FMassEntityManager* EntityManager = MassAPI->GetEntityManager())
			{
				FMassBattleFogVisionSourceFragment FogPolicy;
				FogPolicy.bProvidesVision = true;
				FogPolicy.VisibilityPolicy =
					EMassBattleFogVisibilityPolicy::AlwaysFogVisible;
				UMassAPISubsystem::SetConstSharedFragment(
					*CityData,
					FogPolicy,
					*EntityManager);
			}
		}

		if (UMassBattleSubsystem* MassBattle =
			UMassBattleSubsystem::GetPtr(this);
			MassBattle && MassBattle->bNetworkedMode)
		{
			if (UMassBattleNetworkSubsystem* Network =
				UMassBattleNetworkSubsystem::GetPtr(this))
			{
				CityData->AddFragment_GetRef<FNetworking>().Key =
					FName(*(BenchmarkDataAsset->GetPathName()
						+ TEXT("_CitySingle")));
				CityData->AddTag<FNetworkTag>();
				FEntityTemplateData RegisteredCityTemplate;
				Network->RegisterNetworkTemplateData(
					this,
					CityTemplate,
					RegisteredCityTemplate);
			}
		}

		FAgentSpawnRectangleShapeData ShapeData;
		ShapeData.Region = FVector2D::ZeroVector;
		TArray<FEntityHandle> Handles;
		Handles.Reserve(Locations.Num());
		const FHealth* CityHealth = CityData->GetMutableFragment<FHealth>();
		const float InitialCityHealth = CityHealth
			? CityHealth->Current
			: 100.0f;
		for (const FVector& Location : Locations)
		{
			const TArray<FEntityHandle> Spawned =
				AgentSub->SpawnAgentsByTemplateRectangular(
					CityTemplate,
					1,
					Team,
					Location,
					ShapeData,
					FVector2D::ZeroVector,
					EInitialRotation::CustomRotation,
					FRotator::ZeroRotator);
			if (Spawned.Num() != 1)
			{
				UE_LOG(LogLandmarkSystem, Error,
					TEXT("LandmarkSubsystem: Failed to spawn single-entity City [%s] at %s."),
					*TypeName,
					*Location.ToString());
				continue;
			}
			Handles.Add(Spawned[0]);
			CityHealthSamples.Add(Spawned[0], InitialCityHealth);
		}

		return Handles;
	}

    // City is split into two Mass agents:
    // - gameplay/base agent: one-cell collider, health, selection, no mesh;
    // - visual flag agent: uniformly scaled to 64uu and attached at the base origin.
    // Keeping those responsibilities separate means the flag can change size and
    // animate without shrinking or moving the gameplay footprint.
    FEntityTemplateData GameplayTemplate =
        AgentSub->MakeAgentTemplateDataFromDataAsset(DataAsset);
    FEntityTemplateData FlagTemplate =
        AgentSub->MakeAgentTemplateDataFromDataAsset(DataAsset);
    FMassEntityTemplateData* GameplayData = GameplayTemplate.Get();
    FMassEntityTemplateData* FlagData = FlagTemplate.Get();
    if (!GameplayData || !FlagData)
    {
        UE_LOG(LogLandmarkSystem, Error,
            TEXT("LandmarkSubsystem: Failed to build split City templates for [%s]."),
            *TypeName);
        return {};
    }

    auto ApplyCityStyle = [Cfg, this](FMassEntityTemplateData& TemplateData)
    {
        if (FStyleType* Style = TemplateData.GetMutableFragment<FStyleType>())
        {
            Style->Index = FMath::Clamp(Cfg->VisualLevel, 1, 5);
            Style->Variant = ActiveFlagSetIndex;
        }
    };
    ApplyCityStyle(*GameplayData);
    ApplyCityStyle(*FlagData);

    // The persistent gameplay/base entity owns capture state and the one-cell
    // footprint, but rendering is delegated to the attached flag entity.
    if (FDeath* Death = GameplayData->GetMutableFragment<FDeath>())
    {
        Death->bEnable = false;
    }
    if (FVisualize* Visualize = GameplayData->GetMutableFragment<FVisualize>())
    {
        Visualize->bEnable = false;
    }
    if (FMassBattleISKMAddonFragment* ISKM =
        GameplayData->GetMutableFragment<FMassBattleISKMAddonFragment>())
    {
        ISKM->bEnable = false;
    }

    // The attached entity is visual-only. Its source model already authors the
    // base at 16x16uu and the flag/pole at 64uu, so it is rendered 1:1. Keep the
    // parent's authored capsule dimensions on the
    // child so the renderer performs the same capsule-center-to-ground pivot
    // placement for both agents. The child does not participate in collision or
    // spatial queries, so these dimensions are render-origin metadata only.
    if (FVisualize* Visualize = FlagData->GetMutableFragment<FVisualize>())
    {
        Visualize->bEnable = true;
        Visualize->Transform.SetLocation(FVector3f::ZeroVector);
        Visualize->Transform.SetRotation(FQuat4f::Identity);
        Visualize->Transform.SetScale3D(FVector3f::OneVector);
    }
    if (FMassBattleISKMAddonFragment* ISKM =
        FlagData->GetMutableFragment<FMassBattleISKMAddonFragment>())
    {
        ISKM->bEnable = true;
    }
    if (FCollider* Collider = FlagData->GetMutableFragment<FCollider>())
    {
        Collider->GridRegisterMode = EGridRegisterMode::None;
        Collider->bEnablePBD = false;
        Collider->bCollideWithAgent = false;
        Collider->bCollideWithObst = false;
        Collider->bPushedByPressureField = false;
    }
    if (FHealth* Health = FlagData->GetMutableFragment<FHealth>())
    {
        Health->Current = FMath::Max(1.0f, Health->Maximum);
        Health->bLockHealth = true;
    }
    if (FHealthBar* HealthBar = FlagData->GetMutableFragment<FHealthBar>())
    {
        HealthBar->bShowHealthBar = false;
        HealthBar->bShowOnSelected = false;
    }
    if (FDeath* Death = FlagData->GetMutableFragment<FDeath>())
    {
        Death->bEnable = false;
    }
    if (FBehavior* Behavior = FlagData->GetMutableFragment<FBehavior>())
    {
        Behavior->OnBirth = EBehaviorChoice::Idle;
        Behavior->OnAppearFinish = EBehaviorChoice::Idle;
        Behavior->OnSleepWake = EBehaviorChoice::Idle;
        Behavior->OnPatrolRoundEnd = EBehaviorChoice::Idle;
        Behavior->OnLostTarget = EBehaviorChoice::Idle;
        Behavior->OnReturnFinish = EBehaviorChoice::Idle;
    }
    if (FMove* Move = FlagData->GetMutableFragment<FMove>())
    {
        Move->bEnable = false;
    }
    if (FAvoidance* Avoidance = FlagData->GetMutableFragment<FAvoidance>())
    {
        Avoidance->bEnable = false;
    }
    if (FAttack* Attack = FlagData->GetMutableFragment<FAttack>())
    {
        Attack->bEnable = false;
    }
    if (FSelect* Select = FlagData->GetMutableFragment<FSelect>())
    {
        Select->bEnable = false;
    }
    if (FAgentEvent* Event = FlagData->GetMutableFragment<FAgentEvent>())
    {
        Event->bEnable = false;
    }

    if (UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this))
	{
		if (FMassEntityManager* EntityManager = MassAPI->GetEntityManager())
		{
            FMassBattleFogVisionSourceFragment GameplayFogPolicy;
            GameplayFogPolicy.bProvidesVision = true;
            GameplayFogPolicy.VisibilityPolicy =
                EMassBattleFogVisibilityPolicy::AlwaysFogVisible;
            UMassAPISubsystem::SetConstSharedFragment(
                *GameplayData,
                GameplayFogPolicy,
                *EntityManager);

            FMassBattleFogVisionSourceFragment FlagFogPolicy;
            FlagFogPolicy.bProvidesVision = false;
            FlagFogPolicy.VisibilityPolicy =
                EMassBattleFogVisibilityPolicy::AlwaysFogVisible;
            UMassAPISubsystem::SetConstSharedFragment(
                *FlagData,
                FlagFogPolicy,
                *EntityManager);
		}
	}

    // The network snapshot registry keys templates by FNetworking.Key. The
    // gameplay and visual agents intentionally share a source DataAsset but no
    // longer share a runtime template, so register distinct keys after applying
    // the split configuration.
    if (UMassBattleSubsystem* MassBattle =
        UMassBattleSubsystem::GetPtr(this);
        MassBattle && MassBattle->bNetworkedMode)
    {
        if (UMassBattleNetworkSubsystem* Network =
            UMassBattleNetworkSubsystem::GetPtr(this))
        {
            const FString KeyPrefix = DataAsset->GetPathName();
            GameplayData->AddFragment_GetRef<FNetworking>().Key =
                FName(*(KeyPrefix + TEXT("_CityGameplay")));
            GameplayData->AddTag<FNetworkTag>();
            FlagData->AddFragment_GetRef<FNetworking>().Key =
                FName(*(KeyPrefix + TEXT("_CityFlag")));
            FlagData->AddTag<FNetworkTag>();

            FEntityTemplateData RegisteredGameplayTemplate;
            FEntityTemplateData RegisteredFlagTemplate;
            Network->RegisterNetworkTemplateData(
                this,
                GameplayTemplate,
                RegisteredGameplayTemplate);
            Network->RegisterNetworkTemplateData(
                this,
                FlagTemplate,
                RegisteredFlagTemplate);
        }
    }

    FAgentSpawnRectangleShapeData ShapeData;
    ShapeData.Region = FVector2D::ZeroVector; // 精确点位，不散布

    TArray<FEntityHandle> AllHandles;
    AllHandles.Reserve(Locations.Num());

    const FHealth* GameplayHealth =
        GameplayData->GetMutableFragment<FHealth>();
    const float InitialGameplayHealth =
        GameplayHealth ? GameplayHealth->Current : 100.0f;

    for (const FVector& Loc : Locations)
    {
        FVector SpawnPos(Loc.X, Loc.Y, Loc.Z);

        const TArray<FEntityHandle> GameplayHandles =
            AgentSub->SpawnAgentsByTemplateRectangular(
            GameplayTemplate, 1, Team, SpawnPos, ShapeData,
            FVector2D::ZeroVector, EInitialRotation::CustomRotation, FRotator::ZeroRotator
        );
        if (GameplayHandles.Num() != 1)
        {
            UE_LOG(LogLandmarkSystem, Error,
                TEXT("LandmarkSubsystem: Failed to spawn gameplay City [%s] at %s."),
                *TypeName,
                *SpawnPos.ToString());
            continue;
        }

        const FEntityHandle CityEntity = GameplayHandles[0];
        const TArray<FEntityHandle> FlagHandles =
            AgentSub->SpawnAgentsByTemplateRectangular(
            FlagTemplate, 1, Team, SpawnPos, ShapeData,
            FVector2D::ZeroVector, EInitialRotation::CustomRotation, FRotator::ZeroRotator
        );
        if (FlagHandles.Num() != 1)
        {
            // Keep the City usable and visible if the secondary visual spawn
            // unexpectedly fails.
            if (UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this))
            {
                if (FVisualize* FallbackVisual =
                    MassAPI->GetFragmentPtr<FVisualize>(CityEntity))
                {
                    FallbackVisual->bEnable = true;
                }
            }
            UE_LOG(LogLandmarkSystem, Error,
                TEXT("LandmarkSubsystem: Failed to spawn attached City flag [%s] at %s; using gameplay visual fallback."),
                *TypeName,
                *SpawnPos.ToString());
            AllHandles.Add(CityEntity);
            CityHealthSamples.Add(CityEntity, InitialGameplayHealth);
            continue;
        }

        const FEntityHandle FlagEntity = FlagHandles[0];
        FAgentHostConfig AttachmentConfig;
        AttachmentConfig.bEnable = true;
        AttachmentConfig.bAttached = true;
        AttachmentConfig.bDespawnWhenNoParent = true;
        UMassBattleFuncLib::AttachAgentToAgent(
            this,
            FlagEntity,
            CityEntity,
            FTransform::Identity,
            AttachmentConfig);

        CityFlagEntities.Add(CityEntity, FlagEntity);
        CityHealthSamples.Add(CityEntity, InitialGameplayHealth);
        AllHandles.Add(CityEntity);
    }

    return AllHandles;
}



void ULandmarkSubsystem::RegisterTypeGrid(const FString& Type, URTSCommandGridAsset* GridAsset)
{
    if (GridAsset)
    {
        TypeGridAssets.Add(Type, GridAsset);
    }
}

URTSCommandGridAsset* ULandmarkSubsystem::GetGridByType(const FString& Type) const
{
    if (const TObjectPtr<URTSCommandGridAsset>* Found = TypeGridAssets.Find(Type))
    {
        return Found->Get();
    }
    return nullptr;
}

FString ULandmarkSubsystem::FindTypeByEntity(FEntityHandle Handle) const
{
	if (const FLandmarkInstanceData* Landmark = FindLandmarkByEntity(Handle))
	{
		return Landmark->Type;
	}
	return FString();
}

FString ULandmarkSubsystem::ResolveLandmarkDisplayName(
	const FLandmarkInstanceData& Data,
	const FString& CultureName) const
{
	for (const FString& Candidate : GetLandmarkCultureCandidates(CultureName))
	{
		if (const FString* LocalizedName =
			FindCultureName(Data.LocalizedNames, Candidate))
		{
			return *LocalizedName;
		}
	}
	return Data.Name;
}

FString ULandmarkSubsystem::GetLandmarkDisplayName(
	const FString& LandmarkID,
	const FString& CultureName) const
{
	if (const FLandmarkInstanceData* Data = RegisteredLandmarks.Find(LandmarkID))
	{
		return ResolveLandmarkDisplayName(*Data, CultureName);
	}
	return FString();
}

const FLandmarkInstanceData* ULandmarkSubsystem::FindLandmarkByEntity(FEntityHandle Handle) const
{
	if (Handle.Index == 0) return nullptr;
	for (const auto& Pair : RegisteredLandmarks)
	{
		const FMassEntityHandle& Stored = Pair.Value.EntityHandle;
		if (Stored.Index == Handle.Index && Stored.SerialNumber == Handle.Serial)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

FLandmarkInstanceData* ULandmarkSubsystem::FindMutableLandmarkByEntity(FEntityHandle Handle)
{
	if (Handle.Index == 0) return nullptr;
	for (auto& Pair : RegisteredLandmarks)
	{
		const FMassEntityHandle& Stored = Pair.Value.EntityHandle;
		if (Stored.Index == Handle.Index && Stored.SerialNumber == Handle.Serial)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

const FLandmarkInstanceData* ULandmarkSubsystem::FindLandmarkByID(const FString& ID) const
{
	return RegisteredLandmarks.Find(ID);
}

FLandmarkInstanceData* ULandmarkSubsystem::FindMutableLandmarkByID(const FString& ID)
{
	return RegisteredLandmarks.Find(ID);
}

void ULandmarkSubsystem::SetCapitalCity(const FString& LandmarkID, int32 TeamIndex)
{
	for (auto& Pair : RegisteredLandmarks)
	{
		FLandmarkInstanceData& Landmark = Pair.Value;
		if (Landmark.Team == TeamIndex)
		{
			Landmark.bIsCapital = Pair.Key == LandmarkID;
		}
	}
}

float ULandmarkSubsystem::GetFactoryBuildCost(const FString& Type) const
{
	if (const ULandmarkSettings* Settings = ULandmarkSettings::Get())
	{
		if (const FCityLevelConfig* Config = Settings->FindCityConfig(Type))
		{
			return Config->FactoryBuildCost;
		}
	}
	return 365.0f;
}

float ULandmarkSubsystem::GetGDPPerFactoryPerSettlement(const FString& Type) const
{
	if (const ULandmarkSettings* Settings = ULandmarkSettings::Get())
	{
		if (const FCityLevelConfig* Config = Settings->FindCityConfig(Type))
		{
			return Config->GDPPerFactoryPerSettlement;
		}
	}
	return 0.0f;
}

bool ULandmarkSubsystem::DebugCaptureFirstCity(const int32 CapturingTeam)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this);
	UMassBattleSubsystem* MassBattle = UMassBattleSubsystem::GetPtr(this);
	FMassEntityManager* EntityManager = MassAPI ? MassAPI->GetEntityManager() : nullptr;
	if (!MassBattle || !EntityManager || CapturingTeam < 0)
	{
		return false;
	}

	FLandmarkInstanceData* TargetLandmark = nullptr;
	for (TPair<FString, FLandmarkInstanceData>& Pair : RegisteredLandmarks)
	{
		if (Pair.Value.Type.StartsWith(TEXT("City")) && EntityManager->IsEntityActive(Pair.Value.EntityHandle))
		{
			TargetLandmark = &Pair.Value;
			break;
		}
	}
	if (!TargetLandmark)
	{
		return false;
	}

	FMassEntityQuery TeamQuery(EntityManager->AsShared());
	TeamQuery.AddRequirement<FTeam>(EMassFragmentAccess::ReadOnly);
	FEntityHandle Instigator;
	for (const FMassEntityHandle& Candidate : TeamQuery.GetMatchingEntityHandles())
	{
		if (Candidate == TargetLandmark->EntityHandle) continue;
		const FTeam* Team = EntityManager->GetFragmentDataPtr<FTeam>(Candidate);
		if (Team && Team->index == CapturingTeam)
		{
			Instigator = FEntityHandle(Candidate);
			break;
		}
	}
	if (Instigator.Index == 0)
	{
		return false;
	}

	const FHealth* Health = EntityManager->GetFragmentDataPtr<FHealth>(TargetLandmark->EntityHandle);
	const FLocating* Location = EntityManager->GetFragmentDataPtr<FLocating>(TargetLandmark->EntityHandle);
	const float HealthBefore = Health ? Health->Current : 0.0f;
	const int32 TeamBefore = TargetLandmark->Team;

	FEntityArray Targets;
	Targets.Entities.Add(FEntityHandle(TargetLandmark->EntityHandle));
	FDamage_Point Damage;
	Damage.Damage = FMath::Max(1000000.0f, HealthBefore + 1.0f);
	Damage.PercentDmg = 0.0f;
	Damage.CritProbability = 0.0f;
	FDebuff_Point Debuff;
	Debuff.bEnable = false;
	TArray<FDmgResult> Results;
	MassBattle->ApplyPointDamageAndDebuff(
		Results, Targets, FEntityArray(), Instigator, Instigator,
		Location ? Location->Location : FVector::ZeroVector, Damage, Debuff);

	UE_LOG(LogLandmarkSystem, Display,
		TEXT("City capture validation queued: [%s] Team %d, Health %.1f -> lethal hit by Team %d (Instigator %d:%d, Results=%d)."),
		*TargetLandmark->Name, TeamBefore, HealthBefore, CapturingTeam,
		Instigator.Index, Instigator.Serial, Results.Num());
	return Results.Num() > 0;
#endif
}

void ULandmarkSubsystem::ResolveMassCommandGrid(
	UObject* WorldContextObject,
	const FString& ActiveKey,
	const FRTSSelectionView& SelectionView,
	URTSCommandGridAsset*& OutGrid)
{
	if (OutGrid || !WorldContextObject || WorldContextObject->GetWorld() != GetWorld()) return;
	if (!ActiveKey.Equals(TEXT("Landmark.City"), ESearchCase::IgnoreCase)) return;

	auto TryResolve = [this, &OutGrid](const FRTSUnitData& UnitData)
	{
		if (const FLandmarkInstanceData* Landmark = FindLandmarkByEntity(UnitData.EntityHandle))
		{
			OutGrid = GetGridByType(Landmark->Type);
			if (!OutGrid) OutGrid = GetGridByType(TEXT("City"));
		}
	};

	TryResolve(SelectionView.SingleUnit);
	for (const FRTSUnitData& Item : SelectionView.Items)
	{
		if (OutGrid) break;
		TryResolve(Item);
	}
}

void ULandmarkSubsystem::EnrichMassUnitData(
	UObject* WorldContextObject,
	const FEntityHandle& Entity,
	FRTSUnitData& Data)
{
	if (!WorldContextObject || WorldContextObject->GetWorld() != GetWorld()) return;
	const FLandmarkInstanceData* Landmark = FindLandmarkByEntity(Entity);
	if (!Landmark) return;

	Data.Name = TEXT("City");
	Data.GroupKey = TEXT("Landmark.City");
	Data.TypeKey = TEXT("Landmark.City");
	Data.Role = FString::Printf(TEXT("%s · %s · %d 工厂"),
		Landmark->Name.IsEmpty() ? TEXT("未命名城市") : *Landmark->Name,
		*Landmark->Type,
		Landmark->FactoryCount);
}

void ULandmarkSubsystem::Tick(float DeltaTime)
{
	if (UWorld* World = GetWorld())
	{
		GCityTopologyBenchmark.Tick(*World);
	}
	SyncCityFlagHitAnimations();

	OwnershipPollAccumulator += DeltaTime;
	if (OwnershipPollAccumulator < OwnershipPollInterval) return;
	OwnershipPollAccumulator = 0.0f;
	PollCityOwnership();
}

TStatId ULandmarkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULandmarkSubsystem, STATGROUP_Tickables);
}

bool ULandmarkSubsystem::IsTickable() const
{
	return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld();
}

void ULandmarkSubsystem::SyncCityFlagHitAnimations()
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem =
		World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!EntitySubsystem)
	{
		return;
	}

	FMassEntityManager& EntityManager =
		EntitySubsystem->GetMutableEntityManager();
	for (const TPair<FEntityHandle, FEntityHandle>& Pair : CityFlagEntities)
	{
		const FEntityHandle& CityEntity = Pair.Key;
		const FEntityHandle& FlagEntity = Pair.Value;
		if (!EntityManager.IsEntityActive(CityEntity)
			|| !EntityManager.IsEntityActive(FlagEntity))
		{
			continue;
		}

		const FHealth* Health =
			EntityManager.GetFragmentDataPtr<FHealth>(CityEntity);
		if (!Health)
		{
			continue;
		}

		float* PreviousHealth = CityHealthSamples.Find(CityEntity);
		if (!PreviousHealth)
		{
			CityHealthSamples.Add(CityEntity, Health->Current);
			continue;
		}

		const bool bReceivedDamage =
			Health->Current < *PreviousHealth - KINDA_SMALL_NUMBER;
		*PreviousHealth = Health->Current;
		if (bReceivedDamage)
		{
			TriggerCityFlagHitAnimation(FlagEntity);
		}
	}
}

void ULandmarkSubsystem::UpdateCityFlagTeam(
	const FEntityHandle& CityEntity,
	const int32 NewTeamIndex)
{
	const FEntityHandle* FlagEntity = CityFlagEntities.Find(CityEntity);
	if (!FlagEntity)
	{
		return;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem =
		World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!EntitySubsystem)
	{
		return;
	}

	FMassEntityManager& EntityManager =
		EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityActive(*FlagEntity))
	{
		return;
	}

	FTeam* Team = EntityManager.GetFragmentDataPtr<FTeam>(*FlagEntity);
	if (!Team)
	{
		return;
	}

	const int32 PreviousTeamIndex = Team->index;
	Team->PreviousIndex = PreviousTeamIndex;
	Team->index = NewTeamIndex;
	if (PreviousTeamIndex >= 0 && PreviousTeamIndex <= 37)
	{
		UMassBattleTagHelpers::RemoveEntityTeamTagByIndexDeferred(
			World, PreviousTeamIndex, *FlagEntity);
	}
	UMassBattleTagHelpers::AddEntityTeamTagByIndexDeferred(
		World, NewTeamIndex, *FlagEntity);
}

void ULandmarkSubsystem::PollCityOwnership()
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this);
	if (!EntitySubsystem || !MassAPI) return;

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	for (auto& Pair : RegisteredLandmarks)
	{
		FLandmarkInstanceData& Landmark = Pair.Value;
		if (!EntityManager.IsEntityActive(Landmark.EntityHandle)) continue;

		FHealth* Health = EntityManager.GetFragmentDataPtr<FHealth>(Landmark.EntityHandle);
		FDying* Dying = EntityManager.GetFragmentDataPtr<FDying>(Landmark.EntityHandle);
		if (Health && Health->Current <= 0.0f && Dying
			&& MassAPI->IsValid(Dying->Instigator))
		{
			const FTeam* CapturingTeam =
				MassAPI->GetFragmentPtr<FTeam>(Dying->Instigator);
			if (CapturingTeam && CapturingTeam->index > 0
				&& CapturingTeam->index <= 37
				&& CapturingTeam->index != Landmark.Team)
			{
				const int32 PreviousTeam = Landmark.Team;
				const int32 NewTeam = CapturingTeam->index;

				// Restore the persistent city before changing archetype tags. Any
				// fragment pointer becomes invalid after RemoveTag, so all mutable
				// state is reset first.
				const float RestoredHealth = FMath::Max(1.0f, Health->Maximum);
				Health->Current = RestoredHealth;
				Health->DmgResults.Reset();
				Dying->bInitialized = false;
				Dying->bIsSuicide = false;
				Dying->Duration = 0.0f;
				Dying->Time = 0.0f;
				Dying->DeathDissolveTime = 0.0f;
				Dying->DeathAnimTime = 0.0f;
				Dying->DisableCollisionWithAgentTimer = 0.0f;
				Dying->Instigator = FEntityHandle();
				Dying->HitDirection = FVector3f::ZeroVector;

				if (FAnimating* Animating =
					EntityManager.GetFragmentDataPtr<FAnimating>(Landmark.EntityHandle))
				{
					Animating->Dissolve = 0;
					Animating->HitGlow = 0;
				}
				if (FEntityFlagFragment* Flags =
					EntityManager.GetFragmentDataPtr<FEntityFlagFragment>(Landmark.EntityHandle))
				{
					auto ClearBattleFlag = [Flags](const EBattleFlags Flag)
					{
						Flags->ClearFlag(static_cast<EEntityFlags>(Flag));
					};
					ClearBattleFlag(EBattleFlags::Dying);
					ClearBattleFlag(EBattleFlags::DeathAnim);
					ClearBattleFlag(EBattleFlags::DeathDissolve);
					ClearBattleFlag(EBattleFlags::BeingHit);
					ClearBattleFlag(EBattleFlags::HitAnim);
					ClearBattleFlag(EBattleFlags::HitGlow);
					ClearBattleFlag(EBattleFlags::HitJiggle);
					ClearBattleFlag(EBattleFlags::PendingDestroy);
				}

				if (MassAPI->HasTag<FDyingTag>(Landmark.EntityHandle))
				{
					MassAPI->RemoveTag<FDyingTag>(Landmark.EntityHandle);
				}
				if (MassAPI->HasTag<FDestroyingTag>(Landmark.EntityHandle))
				{
					MassAPI->RemoveTag<FDestroyingTag>(Landmark.EntityHandle);
				}

				if (TransferLandmarkTeam(Pair.Key, NewTeam))
				{
					UE_LOG(LogLandmarkSystem, Display,
						TEXT("City [%s] captured by lethal damage: Team %d -> %d; health restored to %.1f."),
						*Landmark.Name,
						PreviousTeam,
						NewTeam,
						RestoredHealth);
				}
				continue;
			}
		}

		const FTeam* Team = EntityManager.GetFragmentDataPtr<FTeam>(Landmark.EntityHandle);
		if (!Team || Team->index == Landmark.Team) continue;

		const int32 PreviousTeam = Landmark.Team;
		Landmark.Team = Team->index;
		BumpLandmarkRevision();
		UpdateCityFlagTeam(Landmark.EntityHandle, Landmark.Team);
		TriggerCityFlagUpdateAnimation(Landmark.EntityHandle);
		OnLandmarkTeamChangedNative.Broadcast(Landmark, PreviousTeam, Landmark.Team);
		UE_LOG(LogLandmarkSystem, Log, TEXT("City [%s] captured: Team %d -> %d."),
			*Landmark.Name, PreviousTeam, Landmark.Team);
	}
}

void ULandmarkSubsystem::TriggerCityFlagUpdateAnimation(const FEntityHandle& Entity)
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem =
		World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this);
	if (!EntitySubsystem || !MassAPI)
	{
		return;
	}

	const FEntityHandle* AttachedFlag = CityFlagEntities.Find(Entity);
	const FEntityHandle AnimationEntity =
		AttachedFlag ? *AttachedFlag : Entity;

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityActive(AnimationEntity))
	{
		return;
	}

	FAppearing* Appearing =
		EntityManager.GetFragmentDataPtr<FAppearing>(AnimationEntity);
	FAnimating* Animating =
		EntityManager.GetFragmentDataPtr<FAnimating>(AnimationEntity);
	FEntityFlagFragment* Flags =
		EntityManager.GetFragmentDataPtr<FEntityFlagFragment>(AnimationEntity);
	if (!Appearing || !Animating || !Flags)
	{
		return;
	}

	// Reuse the authored flag-raise sequence for ownership changes. Reset every
	// runtime field so a second capture can replay the same animation reliably.
	Appearing->bInitialized = false;
	Appearing->bStarted = false;
	Appearing->Time = 0.0f;
	Appearing->AnimTime = 0.0f;
	Appearing->DissolveTime = 0.0f;
	Animating->SelectedAppearAnimIndex = AnimationHelpers::INVALID_ANIM_INDEX;
	Animating->bUpdateAnimState = true;

	const EEntityFlags AppearingFlag =
		static_cast<EEntityFlags>(EBattleFlags::Appearing);
	const EEntityFlags AppearAnimFlag =
		static_cast<EEntityFlags>(EBattleFlags::AppearAnim);
	Flags->ClearFlag(AppearAnimFlag);
	Flags->SetFlag(AppearingFlag);

	// Add the archetype tag last because an immediate archetype change
	// invalidates fragment pointers.
	if (!MassAPI->HasTag<FAppearingTag>(AnimationEntity))
	{
		MassAPI->AddTag<FAppearingTag>(AnimationEntity);
	}
}

void ULandmarkSubsystem::TriggerCityFlagHitAnimation(
	const FEntityHandle& Entity)
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem =
		World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!EntitySubsystem)
	{
		return;
	}

	FMassEntityManager& EntityManager =
		EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityActive(Entity))
	{
		return;
	}

	FBeingHit* BeingHit =
		EntityManager.GetFragmentDataPtr<FBeingHit>(Entity);
	FAnimating* Animating =
		EntityManager.GetFragmentDataPtr<FAnimating>(Entity);
	FEntityFlagFragment* Flags =
		EntityManager.GetFragmentDataPtr<FEntityFlagFragment>(Entity);
	if (!BeingHit || !Animating || !Flags)
	{
		return;
	}

	BeingHit->ResetAnim();
	BeingHit->AnimCooldownTimer = 0.0f;
	Animating->SelectedHitAnimIndex =
		AnimationHelpers::INVALID_ANIM_INDEX;
	Animating->bUpdateAnimState = true;

	const EEntityFlags BeingHitFlag =
		static_cast<EEntityFlags>(EBattleFlags::BeingHit);
	const EEntityFlags HitAnimFlag =
		static_cast<EEntityFlags>(EBattleFlags::HitAnim);
	Flags->ClearFlag(HitAnimFlag);
	Flags->SetFlag(BeingHitFlag);
	Flags->SetFlag(HitAnimFlag);
}

bool ULandmarkSubsystem::TransferLandmarkTeam(
	const FString& LandmarkID,
	const int32 NewTeamIndex)
{
	if (NewTeamIndex < 0 || NewTeamIndex > 37)
	{
		UE_LOG(LogLandmarkSystem, Warning,
			TEXT("City transfer rejected for [%s]: Team %d has no Mass team tag."),
			*LandmarkID,
			NewTeamIndex);
		return false;
	}

	FLandmarkInstanceData* Landmark = RegisteredLandmarks.Find(LandmarkID);
	if (!Landmark)
	{
		UE_LOG(LogLandmarkSystem, Warning,
			TEXT("City transfer rejected: landmark [%s] does not exist."),
			*LandmarkID);
		return false;
	}
	const int32 PreviousTeam = Landmark->Team;
	if (PreviousTeam == NewTeamIndex)
	{
		return true;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* EntitySubsystem =
		World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!EntitySubsystem)
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityActive(Landmark->EntityHandle))
	{
		UE_LOG(LogLandmarkSystem, Warning,
			TEXT("City transfer rejected for [%s]: Mass entity is not active."),
			*LandmarkID);
		return false;
	}
	FTeam* Team = EntityManager.GetFragmentDataPtr<FTeam>(Landmark->EntityHandle);
	if (!Team)
	{
		return false;
	}

	Team->PreviousIndex = PreviousTeam;
	Team->index = NewTeamIndex;
	UMassBattleTagHelpers::RemoveEntityTeamTagByIndexDeferred(
		World, PreviousTeam, Landmark->EntityHandle);
	UMassBattleTagHelpers::AddEntityTeamTagByIndexDeferred(
		World, NewTeamIndex, Landmark->EntityHandle);
	Landmark->Team = NewTeamIndex;
	BumpLandmarkRevision();
	UpdateCityFlagTeam(Landmark->EntityHandle, NewTeamIndex);
	TriggerCityFlagUpdateAnimation(Landmark->EntityHandle);
	OnLandmarkTeamChangedNative.Broadcast(*Landmark, PreviousTeam, NewTeamIndex);
	UE_LOG(LogLandmarkSystem, Display,
		TEXT("City [%s] transferred: Team %d -> %d."),
		*Landmark->Name,
		PreviousTeam,
		NewTeamIndex);
	return true;
}

void ULandmarkSubsystem::BumpLandmarkRevision()
{
	++LandmarkRevision;
	if (LandmarkRevision == 0)
	{
		LandmarkRevision = 1;
	}
}

void ULandmarkSubsystem::Deinitialize()
{
	URTSSelectionSubsystem::OnResolveMassCommandGrid().Remove(CommandGridResolverHandle);
	URTSSelectionSubsystem::OnEnrichMassUnitData().Remove(UnitDataEnricherHandle);
	OnLandmarkTeamChangedNative.Clear();
	RegisteredLandmarks.Empty();
	CityFlagEntities.Empty();
	CityHealthSamples.Empty();
	SpatialGrid.Empty();
	Super::Deinitialize();
}

void ULandmarkSubsystem::RegisterLandmark(const FLandmarkInstanceData& Data)
{
	FString SafeID = Data.ID;
	if (SafeID.IsEmpty())
	{
		SafeID = FGuid::NewGuid().ToString();
	}
    
    // Check if exists
    if (RegisteredLandmarks.Contains(SafeID))
    {
        FLandmarkInstanceData& Existing = RegisteredLandmarks[SafeID];
        if (Data.LinkedActor.IsValid())
        {
            Existing.LinkedActor = Data.LinkedActor;
            if (!Data.Name.IsEmpty()) Existing.Name = Data.Name;
            if (Existing.Value == 0 && Data.Value > 0) Existing.Value = Data.Value;
        }
        return;
    }
	
	FLandmarkInstanceData NewData = Data;
	NewData.ID = SafeID;

	// 纯数据注册，城市 Agent 的 Mass Entity 由 SpawnCityAgents 统一创建
	RegisteredLandmarks.Add(SafeID, NewData);
	BumpLandmarkRevision();

    // 更新空间格网
    if (SpatialCellSize > 0)
    {
        FVector Loc = NewData.GetLocation();
        FIntPoint Cell(FMath::FloorToInt(Loc.X / SpatialCellSize), FMath::FloorToInt(Loc.Y / SpatialCellSize));
        SpatialGrid.FindOrAdd(Cell).AddUnique(SafeID);
    }
}



void ULandmarkSubsystem::UpdateLandmark(const FString& ID, const FLandmarkInstanceData& NewData)
{
	if (RegisteredLandmarks.Contains(ID))
	{
		RegisteredLandmarks[ID] = NewData;
		BumpLandmarkRevision();
	}
}

void ULandmarkSubsystem::UnregisterLandmark(const FString& ID)
{
    // Remove from Spatial Grid first (while we have Data)
    if (FLandmarkInstanceData* Data = RegisteredLandmarks.Find(ID))
    {
        if (SpatialCellSize > 0)
        {
             FVector Loc = Data->GetLocation();
             FIntPoint Cell(FMath::FloorToInt(Loc.X / SpatialCellSize), FMath::FloorToInt(Loc.Y / SpatialCellSize));
             
             if (TArray<FString>* List = SpatialGrid.Find(Cell))
             {
                 List->Remove(ID);
             }
        }
    }

	if (RegisteredLandmarks.Remove(ID) > 0)
	{
		BumpLandmarkRevision();
	}
}

void ULandmarkSubsystem::UnregisterAll()
{
	if (!RegisteredLandmarks.IsEmpty())
	{
		RegisteredLandmarks.Empty();
		BumpLandmarkRevision();
	}
}

bool ULandmarkSubsystem::LoadLandmarksFromFile(const FString& FileName)
{
    const FString MapDataDirectory =
        FPaths::ProjectContentDir() / TEXT("MapData");
    const FString BaseLandmarkPath = MapDataDirectory / FileName;
    FString RelativePath = BaseLandmarkPath;

    // A culture-specific full map is the most direct way to consume the
    // multilingual city package. Only presentation fields are expected to
    // differ; stable IDs below deliberately use the configured base file and
    // record index instead of the localized name.
    for (const FString& Culture : GetLandmarkCultureCandidates(FString()))
    {
        if (!IsSafeCultureDirectory(Culture))
        {
            continue;
        }

        const FString CulturePath = MapDataDirectory / Culture / FileName;
        if (FPaths::FileExists(CulturePath))
        {
            RelativePath = CulturePath;
            UE_LOG(LogLandmarkSystem, Log,
                TEXT("LandmarkSubsystem: culture [%s] selected landmark file [%s]."),
                *Culture,
                *CulturePath);
            break;
        }
    }

    FString JsonString;
    
    if (!FFileHelper::LoadFileToString(JsonString, *RelativePath))
    {
        UE_LOG(LogLandmarkSystem, Warning, TEXT("LandmarkSubsystem: Failed to load file %s"), *RelativePath);
        return false;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    TArray<TSharedPtr<FJsonValue>> JsonArray;

    if (FJsonSerializer::Deserialize(Reader, JsonArray))
    {
        RegisteredLandmarks.Reset();
        BumpLandmarkRevision();
        SpatialGrid.Reset();
        const ULandmarkSettings* Settings = ULandmarkSettings::Get();

        // Preserve the legacy no-ID hash across localization. A localized full
        // file may change Name, but its identity name comes from the configured
        // base map at the same record index.
        TArray<FString> BaseIdentityNames;
        if (!RelativePath.Equals(BaseLandmarkPath, ESearchCase::IgnoreCase))
        {
            FString BaseJsonString;
            TArray<TSharedPtr<FJsonValue>> BaseJsonArray;
            if (FFileHelper::LoadFileToString(
                    BaseJsonString,
                    *BaseLandmarkPath)
                && FJsonSerializer::Deserialize(
                    TJsonReaderFactory<>::Create(BaseJsonString),
                    BaseJsonArray))
            {
                BaseIdentityNames.SetNum(BaseJsonArray.Num());
                for (int32 BaseIndex = 0;
                    BaseIndex < BaseJsonArray.Num();
                    ++BaseIndex)
                {
                    const TSharedPtr<FJsonObject>* BaseObjectPtr = nullptr;
                    if (BaseJsonArray[BaseIndex].IsValid()
                        && BaseJsonArray[BaseIndex]->TryGetObject(BaseObjectPtr)
                        && BaseObjectPtr)
                    {
                        (*BaseObjectPtr)->TryGetStringField(
                            TEXT("Name"),
                            BaseIdentityNames[BaseIndex]);
                    }
                }
            }
        }

        TArray<FLandmarkInstanceData> LoadedLandmarks;
        LoadedLandmarks.Reserve(JsonArray.Num());
        for (int32 RecordIndex = 0; RecordIndex < JsonArray.Num(); ++RecordIndex)
        {
            const TSharedPtr<FJsonValue>& Value = JsonArray[RecordIndex];
            const TSharedPtr<FJsonObject>* ObjectPtr;
            if (Value->TryGetObject(ObjectPtr) && ObjectPtr)
            {
                FLandmarkInstanceData Data;
                FJsonObjectConverter::JsonObjectToUStruct((*ObjectPtr).ToSharedRef(), &Data);
                
                // Correct Coordinate System Mapping: X=Forward(North), Y=Right(East)
                Data.X = (double)(*ObjectPtr)->GetNumberField(TEXT("Y")); 
                Data.Y = (double)(*ObjectPtr)->GetNumberField(TEXT("X"));
                
                // Stable fallback ID: loading the same map again must identify the same city.
                if (Data.ID.IsEmpty())
                {
                    const FString& BaseIdentityName =
                        BaseIdentityNames.IsValidIndex(RecordIndex)
                        && !BaseIdentityNames[RecordIndex].IsEmpty()
                            ? BaseIdentityNames[RecordIndex]
                            : Data.Name;
                    const FString StableKey = FString::Printf(
                        TEXT("%s|%s|%.3f|%.3f"),
                        *Data.Type,
                        *BaseIdentityName,
                        Data.X,
                        Data.Y);
                    Data.ID = FString::Printf(TEXT("City_%08X"), FCrc::StrCrc32(*StableKey));
                }

                if (Settings && Settings->bForceNeutralCityOwnership)
                {
                    Data.Team = 0;
                }
                
                // Assign default VP for Cities if missing
                if (Data.Value == 0)
                {
                    Data.Value = GetDefaultVictoryPoints(Data.Type);
                }
                Data.FactoryCount = FMath::Max(Data.FactoryCount, Data.Value);
                LoadedLandmarks.Add(MoveTemp(Data));
            }
        }

        ApplyCityNameLocalizationTable(FileName, LoadedLandmarks);
        for (const FLandmarkInstanceData& Data : LoadedLandmarks)
        {
            RegisterLandmark(Data);
        }

        return true;
    }
    
    UE_LOG(LogTemp, Error, TEXT("LandmarkSubsystem: Failed to parse JSON from %s"), *FileName);
    return false;
}

void ULandmarkSubsystem::ApplyCityNameLocalizationTable(
    const FString& LandmarkFileName,
    TArray<FLandmarkInstanceData>& Landmarks) const
{
    const ULandmarkSettings* Settings = ULandmarkSettings::Get();
    if (!Settings || Settings->CityNameLocalizationTableFile.IsEmpty())
    {
        return;
    }

    const FString TablePath =
        FPaths::ProjectContentDir()
        / TEXT("MapData")
        / Settings->CityNameLocalizationTableFile;
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *TablePath))
    {
        UE_LOG(LogLandmarkSystem, Verbose,
            TEXT("LandmarkSubsystem: optional city localization table not found [%s]."),
            *TablePath);
        return;
    }

    TArray<TSharedPtr<FJsonValue>> Rows;
    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, Rows))
    {
        UE_LOG(LogLandmarkSystem, Warning,
            TEXT("LandmarkSubsystem: failed to parse city localization table [%s]."),
            *TablePath);
        return;
    }

    static const TCHAR* CultureFields[] =
    {
        TEXT("en"),
        TEXT("zh-Hans"),
        TEXT("zh-Hant"),
        TEXT("ru"),
        TEXT("ja"),
        TEXT("ko")
    };

    const FString CleanLandmarkFile =
        FPaths::GetCleanFilename(LandmarkFileName);
    int32 AppliedNameCount = 0;
    for (const TSharedPtr<FJsonValue>& RowValue : Rows)
    {
        const TSharedPtr<FJsonObject>* RowObjectPtr = nullptr;
        if (!RowValue.IsValid()
            || !RowValue->TryGetObject(RowObjectPtr)
            || !RowObjectPtr)
        {
            continue;
        }

        const TSharedPtr<FJsonObject>& Row = *RowObjectPtr;
        FString RowMapFile;
        double RecordIndexNumber = -1.0;
        if (!Row->TryGetStringField(TEXT("MapFile"), RowMapFile)
            || !FPaths::GetCleanFilename(RowMapFile).Equals(
                CleanLandmarkFile,
                ESearchCase::IgnoreCase)
            || !Row->TryGetNumberField(
                TEXT("RecordIndex"),
                RecordIndexNumber))
        {
            continue;
        }

        const int32 RecordIndex = FMath::RoundToInt(RecordIndexNumber);
        if (!Landmarks.IsValidIndex(RecordIndex))
        {
            continue;
        }

        FLandmarkInstanceData& Landmark = Landmarks[RecordIndex];
        for (const TCHAR* CultureField : CultureFields)
        {
            FString LocalizedName;
            if (Row->TryGetStringField(CultureField, LocalizedName)
                && !LocalizedName.IsEmpty()
                && !LocalizedName.Equals(Landmark.Name, ESearchCase::CaseSensitive))
            {
                Landmark.LocalizedNames.FindOrAdd(FString(CultureField)) =
                    MoveTemp(LocalizedName);
                ++AppliedNameCount;
            }
        }
    }

    UE_LOG(LogLandmarkSystem, Log,
        TEXT("LandmarkSubsystem: applied %d localized city names from [%s] for [%s]."),
        AppliedNameCount,
        *TablePath,
        *CleanLandmarkFile);
}

bool ULandmarkSubsystem::SaveLandmarksToFile(const FString& FileName, const TArray<FLandmarkInstanceData>& DataToSave)
{
    FString RelativePath = FPaths::ProjectContentDir() / TEXT("MapData") / FileName;
    
    TArray<TSharedPtr<FJsonValue>> JsonArray;
    
    // Manual serialization because UStructArrayToJson might not exist or isn't exposed correctly
    for (const FLandmarkInstanceData& Data : DataToSave)
    {
        TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
        if (FJsonObjectConverter::UStructToJsonObject(FLandmarkInstanceData::StaticStruct(), &Data, JsonObj, 0, 0))
        {
            JsonArray.Add(MakeShared<FJsonValueObject>(JsonObj));
        }
    }
    
    if (JsonArray.Num() > 0)
    {
        FString JsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
        if (FJsonSerializer::Serialize(JsonArray, Writer))
        {
            if (FFileHelper::SaveStringToFile(JsonString, *RelativePath))
            {
                UE_LOG(LogTemp, Log, TEXT("LandmarkSubsystem: Saved %d landmarks to %s"), DataToSave.Num(), *RelativePath);
                return true;
            }
        }
    }
    
    UE_LOG(LogTemp, Error, TEXT("LandmarkSubsystem: Failed to save JSON to %s"), *RelativePath);
    return false;
}

// --- Spatial Grid Implementation ---

void ULandmarkSubsystem::RebuildSpatialGrid()
{
    SpatialGrid.Empty();
    // This grid is a broadphase only; exact distance and projection checks run
    // below. A coarse cell avoids thousands of empty TMap probes per camera
    // update without reducing label placement precision.
    SpatialCellSize = 10000.0f;

    for (const auto& Pair : RegisteredLandmarks)
    {
        const FLandmarkInstanceData& Data = Pair.Value;
        FVector Loc = Data.GetLocation();
        if (Data.LinkedActor.IsValid()) 
        {
            Loc = Data.LinkedActor->GetActorLocation();
        }
        
        FIntPoint Cell(FMath::FloorToInt(Loc.X / SpatialCellSize), FMath::FloorToInt(Loc.Y / SpatialCellSize));
        
        // Native TMap allows referencing Value directly
        TArray<FString>& List = SpatialGrid.FindOrAdd(Cell);
        List.Add(Data.ID);
    }
}

void ULandmarkSubsystem::UpdateCameraState(const FVector& CameraLocation, const FRotator& CameraRotation, float FOV, float ZoomFactor)
{
    // Optimization: Skip if camera stable (User Request: "Simply cache it!")
    // If camera hasn't moved significant distance or rotated
    if (FVector::DistSquared(CameraLocation, LastCameraLoc) < 1.0f && CameraRotation.Equals(LastCameraRot, 0.01f))
    {
        return; 
    }

	LastCameraLoc = CameraLocation;
	LastCameraRot = CameraRotation;

	VisibleLandmarkIDs.Reset();
	CachedScreenPositions.Reset();
	CachedScales.Reset();
	CachedAlphas.Reset();

    // Lazy Build
    if (SpatialGrid.Num() == 0 && RegisteredLandmarks.Num() > 0)
    {
        RebuildSpatialGrid();
    }

	// Calculate Visible Cell Range
    // Simple heuristic: frustum roughly covers Height * AspectRatio on ground.
    // Assume max aspect 2.0 (Ultrawide). Radius ~= Height * 1.5.
    float SearchRadius = FMath::Max(20000.0f, CameraLocation.Z * 2.0f); 
    int32 CellRadius = FMath::CeilToInt(SearchRadius / SpatialCellSize);
    const double SearchRadiusSquared =
        FMath::Square(static_cast<double>(SearchRadius));

    FIntPoint CenterCell(FMath::FloorToInt(CameraLocation.X / SpatialCellSize), FMath::FloorToInt(CameraLocation.Y / SpatialCellSize));
    
    // Cache the height above each landmark's ground point once outside the
    // loops. This is a relative offset, never an absolute world Z.
    float LabelHeightOffset = 64.0f;
    if (const ULandmarkSettings* Settings = ULandmarkSettings::Get())
    {
        LabelHeightOffset = Settings->CityLabelZOffset;
    }

    // Iterate neighbor cells
    for (int32 x = -CellRadius; x <= CellRadius; ++x)
    {
        for (int32 y = -CellRadius; y <= CellRadius; ++y)
        {
            FIntPoint TargetCell = CenterCell + FIntPoint(x, y);
            if (TArray<FString>* ListPtr = SpatialGrid.Find(TargetCell))
            {
                // Iterate Landmarks in this cell
                for (const FString& ID : *ListPtr)
                {
                    FLandmarkInstanceData* DataPtr = RegisteredLandmarks.Find(ID);
                    if (!DataPtr) continue;
                    
                    const FLandmarkInstanceData& Data = *DataPtr;

                    const FVector LandmarkLocation = Data.GetLocation();
                    if (FVector2D::DistSquared(
                            FVector2D(CameraLocation),
                            FVector2D(LandmarkLocation))
                        > SearchRadiusSquared)
                    {
                        continue;
                    }

                    // 0. Height Filtering
                    float CamZ = CameraLocation.Z;
                    if (CamZ < Data.ZMin || CamZ > Data.ZMax)
                    {
                        continue;
                    }

                    FVector FinalLocation =
                        LandmarkLocation + FVector(0.0f, 0.0f, LabelHeightOffset);
                    FinalLocation += Data.VisualOffset;

                    // 2. Project
                    FVector2D ScreenPos;
                    if (ProjectWorldLocationToScreen(FinalLocation, ScreenPos))
                    {
                        VisibleLandmarkIDs.Add(ID);
                        CachedScreenPositions.Add(ScreenPos);

                        // --- Dynamic Scaling ---
                        // Evaluate scale based on distance using curve
                        float ScaleFactor = 1.0f;
                        if (ScaleCurve.GetRichCurve() && !ScaleCurve.GetRichCurve()->IsEmpty())
                        {
                            float Distance = FVector::Dist(CameraLocation, FinalLocation);
                            ScaleFactor = ScaleCurve.GetRichCurve()->Eval(Distance);
                        }
                        CachedScales.Add(ScaleFactor);

                        // --- Alpha Fading ---
                        float AlphaFactor = 1.0f;
                        if (AlphaCurve.GetRichCurve() && !AlphaCurve.GetRichCurve()->IsEmpty())
                        {
                            float Distance = FVector::Dist(CameraLocation, FinalLocation);
                            AlphaFactor = AlphaCurve.GetRichCurve()->Eval(Distance);
                        }
                        CachedAlphas.Add(AlphaFactor);
                    }
                }
            }
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
			FLandmarkInstanceData LocalizedCopy = *Ptr;
			LocalizedCopy.Name = ResolveLandmarkDisplayName(*Ptr);
			OutVisibleLandmarks.Add(MoveTemp(LocalizedCopy));
		}
		else
		{
			// Should not happen, but keep arrays synced
			OutVisibleLandmarks.Add(FLandmarkInstanceData()); 
		}
	}
}

// SpawnMissingCities removed (inlined in OnWorldBeginPlay)

void ULandmarkSubsystem::DrawLandmarks(UCanvas* InCanvas)
{
    if (!InCanvas) return;

    // Use cached data directly
    for (int32 i = 0; i < VisibleLandmarkIDs.Num(); ++i)
    {
        if (!CachedScreenPositions.IsValidIndex(i)) continue;

        FString ID = VisibleLandmarkIDs[i];
        FLandmarkInstanceData* DataPtr = RegisteredLandmarks.Find(ID);
        if (!DataPtr) continue;

        const FLandmarkInstanceData& Data = *DataPtr;
        const FVector2D& ScreenPos = CachedScreenPositions[i];
        
        // --- Scale Calculation ---
        float OriginalScale = CachedScales[i];
        float SettingsBaseScale = 1.0f;
        if (const ULandmarkSettings* Settings = ULandmarkSettings::Get())
        {
            SettingsBaseScale = Settings->BaseFontScale;
        }

        // Apply base scale from settings + dynamic scale from curve
        float VisualScale = OriginalScale * SettingsBaseScale; 
        float Alpha = CachedAlphas[i];
        
        if (Alpha <= 0.01f) continue;

        // resolve Fonts
        UFont* UseNameFont = NameFont ? NameFont.Get() : GEngine->GetLargeFont();
        UFont* UseVPFontTarget = VPFont ? VPFont.Get() : UseNameFont; // Default to NameFont if VPFont missing
        const FString DisplayName = ResolveLandmarkDisplayName(Data);

        // --- 1. Measure Name ---
        FCanvasTextItem NameItem(FVector2D::ZeroVector, FText::FromString(DisplayName), UseNameFont, FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
        NameItem.Scale = FVector2D(VisualScale, VisualScale);
        NameItem.EnableShadow(FLinearColor::Black);
        
        float NXL, NYL;
        InCanvas->StrLen(UseNameFont, DisplayName, NXL, NYL);
        NameItem.DrawnSize = FVector2D(NXL, NYL);
        FVector2D NameSizeScaled = NameItem.DrawnSize * NameItem.Scale;

        // --- 2. Measure VP (if any) ---
        bool bHasVP = (Data.Value > 0);
        FCanvasTextItem VPItem(FVector2D::ZeroVector, FText::GetEmpty(), UseVPFontTarget, FLinearColor(1.0f, 0.84f, 0.0f, Alpha));
        FVector2D VPSizeScaled = FVector2D::ZeroVector;

        if (bHasVP)
        {
             FString VPString = FString::Printf(TEXT("%d 胜利点"), Data.Value);
             VPItem.Text = FText::FromString(VPString);
             VPItem.Scale = FVector2D(VisualScale * 0.8f, VisualScale * 0.8f);
             VPItem.EnableShadow(FLinearColor::Black);
             
             float VXL, VYL;
             InCanvas->StrLen(UseVPFontTarget, VPString, VXL, VYL);
             VPItem.DrawnSize = FVector2D(VXL, VYL);
             VPSizeScaled = VPItem.DrawnSize * VPItem.Scale;
        }

        // --- 3. Layout Stack (Bottom-Up Anchor) ---
        // Anchor is ScreenPos (The Roof). We stack upwards: [Roof] <- [VP] <- [Name]
        float CurrentY = ScreenPos.Y;
        
        // Stack VP first (Bottom element)
        if (bHasVP)
        {
            CurrentY -= VPSizeScaled.Y;
            FVector2D VPPos(ScreenPos.X - (VPSizeScaled.X * 0.5f), CurrentY);
            
            // Pixel Snap
            VPPos.X = FMath::RoundToFloat(VPPos.X);
            VPPos.Y = FMath::RoundToFloat(VPPos.Y);
            
            VPItem.Position = VPPos;
            InCanvas->DrawItem(VPItem);
        }

        // Stack Name second (Top element)
        CurrentY -= NameSizeScaled.Y;
        FVector2D NamePos(ScreenPos.X - (NameSizeScaled.X * 0.5f), CurrentY);
        
        // Pixel Snap
        NamePos.X = FMath::RoundToFloat(NamePos.X);
        NamePos.Y = FMath::RoundToFloat(NamePos.Y);
        
        NameItem.Position = NamePos;
        InCanvas->DrawItem(NameItem);
        
    } // End Loop
    
    // DEBUG
    // if (GEngine)
    // {
    //     FString Stats = FString::Printf(TEXT("Landmarks: Total %d | Visible %d"), RegisteredLandmarks.Num(), VisibleLandmarkIDs.Num());
    //     InCanvas->DrawText(GEngine->GetLargeFont(), Stats, 100, 100);
    // }
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
