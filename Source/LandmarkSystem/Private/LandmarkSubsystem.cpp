#include "LandmarkSubsystem.h"
#include "LandmarkSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Data/RTSCommandGridAsset.h"
#include "Commands/RTSCityCommands.h"
// MassBattle
#include "Subsystems/MassBattleAgentSubsystem.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "MassBattleStructs.h"

DEFINE_LOG_CATEGORY(LogLandmarkSystem);

void ULandmarkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
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
    // 1. 加载 JSON 地标数据
    FString MapName = InWorld.GetName();
    MapName.RemoveFromStart(InWorld.StreamingLevelsPrefix);
    UE_LOG(LogLandmarkSystem, Warning, TEXT("LandmarkSubsystem: OnWorldBeginPlay [%s]"), *MapName);

    FString FileName = FString::Printf(TEXT("Landmarks_%s_ZH.json"), *MapName);
    if (!LoadLandmarksFromFile(FileName))
    {
        FileName = FString::Printf(TEXT("Landmarks_%s.json"), *MapName);
        LoadLandmarksFromFile(FileName);
    }

    // 2. 注册城市 Command Grid
    // URTSCityCommandGrid 是 C++ 类而非资产，先创建一个共享实例注册给所有城市类型
    // 如果配置文件里填了资产，则以资产为准（覆盖）
    URTSCityCommandGrid* SharedCityGrid = NewObject<URTSCityCommandGrid>(this);

    const ULandmarkSettings* Settings = ULandmarkSettings::Get();
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
        UE_LOG(LogLandmarkSystem, Log, TEXT("LandmarkSubsystem: Registered city grid for %d types."), Settings->CityLevelConfigs.Num());
    }
    else
    {
        UE_LOG(LogLandmarkSystem, Warning, TEXT("LandmarkSubsystem: No LandmarkSettings found, city grids not registered."));
    }


    // 3. 批量生成所有城市类型的 Mass 实体（一次性，内存高效）
    BatchSpawnAllCities();
}

void ULandmarkSubsystem::BatchSpawnAllCities()
{
    const ULandmarkSettings* Settings = ULandmarkSettings::Get();
    if (!Settings)
    {
        UE_LOG(LogLandmarkSystem, Error, TEXT("LandmarkSubsystem: ULandmarkSettings not found!"));
        return;
    }

    // 按城市等级分组收集坐标
    TMap<FString, TArray<FVector>> TypeToLocations;
    for (auto& Pair : RegisteredLandmarks)
    {
        FLandmarkInstanceData& Data = Pair.Value;
        if (Data.Value == 0) Data.Value = GetDefaultVictoryPoints(Data.Type);
        TypeToLocations.FindOrAdd(Data.Type).Add(FVector(Data.X, Data.Y, 0.0f));
    }

    // 每个等级批量生成一次
    for (const FCityLevelConfig& Cfg : Settings->CityLevelConfigs)
    {
        TArray<FVector>* Locations = TypeToLocations.Find(Cfg.TypeName);
        if (!Locations || Locations->Num() == 0) continue;

        TArray<FEntityHandle> Handles = BatchSpawnCityType(Cfg.TypeName, *Locations);
        UE_LOG(LogLandmarkSystem, Warning, TEXT("LandmarkSubsystem: [%s] Spawned %d/%d entities."),
            *Cfg.TypeName, Handles.Num(), Locations->Num());

        // 将 Handle 写回 RegisteredLandmarks
        int32 HandleIdx = 0;
        for (auto& Pair : RegisteredLandmarks)
        {
            if (HandleIdx >= Handles.Num()) break;
            if (Pair.Value.Type.Equals(Cfg.TypeName, ESearchCase::IgnoreCase))
            {
                Pair.Value.EntityHandle = Handles[HandleIdx++];
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

    // 由资产构建一次统一模板，复用于所有坐标点
    FEntityTemplateData BaseTemplate = AgentSub->MakeTemplateDataFromDataAsset(DataAsset);

    FAgentSpawnRectangleShapeData ShapeData;
    ShapeData.Region = FVector2D::ZeroVector; // 精确点位，不散布

    TArray<FEntityHandle> AllHandles;
    AllHandles.Reserve(Locations.Num());

    for (const FVector& Loc : Locations)
    {
        FVector SpawnPos(Loc.X, Loc.Y, Loc.Z);

        TArray<FEntityHandle> Handles = AgentSub->SpawnAgentsByTemplateRectangular(
            BaseTemplate, 1, Team, SpawnPos, ShapeData,
            FVector2D::ZeroVector, EInitialDirection::CustomDirection, FVector2D::ZeroVector
        );
        AllHandles.Append(Handles);
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
    if (Handle.Index == 0) return FString();
    for (const auto& Pair : RegisteredLandmarks)
    {
        const FEntityHandle& Stored = Pair.Value.EntityHandle;
        if (Stored.Index == Handle.Index && Stored.Serial == Handle.Serial)
        {
            return Pair.Value.Type;
        }
    }
    return FString();
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

	RegisteredLandmarks.Remove(ID);
}

void ULandmarkSubsystem::UnregisterAll()
{
	RegisteredLandmarks.Empty();
}

bool ULandmarkSubsystem::LoadLandmarksFromFile(const FString& FileName)
{
    FString RelativePath = FPaths::ProjectContentDir() / TEXT("MapData") / FileName;
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
        for (const TSharedPtr<FJsonValue>& Value : JsonArray)
        {
            const TSharedPtr<FJsonObject>* ObjectPtr;
            if (Value->TryGetObject(ObjectPtr) && ObjectPtr)
            {
                FLandmarkInstanceData Data;
                FJsonObjectConverter::JsonObjectToUStruct((*ObjectPtr).ToSharedRef(), &Data);
                
                // Coordinate System Fix:
                // JSON Data (GIS/Screen): X=Right(East), Y=Up(North)
                // Unreal Engine: X=Forward(North), Y=Right(East)
                // Mapping: UE.X = Json.Y, UE.Y = Json.X
                double JsonX = Data.X;
                double JsonY = Data.Y;
                Data.X = JsonY; 
                Data.Y = JsonX; 
                
                // Fallback ID
                if (Data.ID.IsEmpty()) Data.ID = FGuid::NewGuid().ToString();
                
                // Assign default VP for Cities if missing
                if (Data.Value == 0)
                {
                    Data.Value = GetDefaultVictoryPoints(Data.Type);
                }
                
                // Default Z-offset for JSON elements
                if (Data.VisualOffset.IsNearlyZero())
                {
                    Data.VisualOffset = FVector(0.0f, 0.0f, 500.0f);
                }

                RegisterLandmark(Data);
            }
        }
        
        FString Msg = FString::Printf(TEXT("LandmarkSystem: Loaded %d landmarks from %s"), RegisteredLandmarks.Num(), *FileName);
        UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, Msg);
        }
        return true;
    }
    
    UE_LOG(LogTemp, Error, TEXT("LandmarkSubsystem: Failed to parse JSON from %s"), *FileName);
    return false;
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
    // User Precision Requirement: 256uu cell size
    SpatialCellSize = 256.0f; 

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
    
    // Clamp radius for the 256uu high-density grid. 
    // Max 40 cells = ~10km search.
    if (CellRadius > 40) CellRadius = 40; 

    FIntPoint CenterCell(FMath::FloorToInt(CameraLocation.X / SpatialCellSize), FMath::FloorToInt(CameraLocation.Y / SpatialCellSize));
    
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

                    // 0. Height Filtering
                    float CamZ = CameraLocation.Z;
                    if (CamZ < Data.ZMin || CamZ > Data.ZMax)
                    {
                        continue;
                    }

                    // 1. Precise Logic
                    FVector Location = Data.GetLocation();
                    
                    if (Data.LinkedActor.IsValid())
                    {
                        Location = Data.LinkedActor->GetActorLocation();
                    }

                    // Use Manual Offset (Configured to City Top)
                    FVector FinalOffset = Data.VisualOffset;
                    
                    Location += FinalOffset;
                    // 2. Project
                    FVector2D ScreenPos;
                    if (ProjectWorldLocationToScreen(Location, ScreenPos))
                    {
                        VisibleLandmarkIDs.Add(ID);
                        CachedScreenPositions.Add(ScreenPos);
                        CachedScales.Add(1.0f);
                        CachedAlphas.Add(1.0f);
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
			OutVisibleLandmarks.Add(*Ptr);
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
        
        // --- 2x Scale ---
        float OriginalScale = CachedScales[i];
        float VisualScale = OriginalScale * 2.0f; // Scale up for visibility
        float Alpha = CachedAlphas[i];
        
        if (Alpha <= 0.01f) continue;

        // resolve Fonts
        UFont* UseNameFont = NameFont ? NameFont.Get() : GEngine->GetLargeFont();
        UFont* UseVPFontTarget = VPFont ? VPFont.Get() : UseNameFont; // Default to NameFont if VPFont missing

        // --- 1. Measure Name ---
        FCanvasTextItem NameItem(FVector2D::ZeroVector, FText::FromString(Data.Name), UseNameFont, FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
        NameItem.Scale = FVector2D(VisualScale, VisualScale);
        NameItem.EnableShadow(FLinearColor::Black);
        
        float NXL, NYL;
        InCanvas->StrLen(UseNameFont, Data.Name, NXL, NYL);
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
    /*
    if (GEngine)
    {
         FString Stats = FString::Printf(TEXT("Landmarks: Total %d | Visible %d"), RegisteredLandmarks.Num(), VisibleLandmarkIDs.Num());
         InCanvas->DrawText(GEngine->GetLargeFont(), Stats, 100, 100);
    }
    */
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
