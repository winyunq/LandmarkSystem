#pragma once

#include "CoreMinimal.h"
#include "LandmarkTypes.generated.h"

/** 
 * Type of landmark for categorization and visual styling 
 * 地标类型，用于分类和视觉样式
 */
UENUM(BlueprintType)
enum class ELandmarkType : uint8
{
    Generic     UMETA(DisplayName = "Generic"),
    City        UMETA(DisplayName = "City"),
    Capital     UMETA(DisplayName = "Capital"),
    Region      UMETA(DisplayName = "Region"),
    Mountain    UMETA(DisplayName = "Mountain"),
    River       UMETA(DisplayName = "River"),
    Strategic   UMETA(DisplayName = "Strategic Point")
};

/**
 * Visual configuration for scaling and LOD
 * 缩放和 LOD 的视觉配置
 */
USTRUCT(BlueprintType)
struct FLandmarkVisualConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BaseScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MinVisibleZoom = 0.0f; // 0.0 = Ground, 1.0 = Space

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxVisibleZoom = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Priority = 0; // Higher shows on top / filters out lower priority

    FLandmarkVisualConfig() {}
};

/**
 * Runtime data for a single landmark instance
 * 单个地标实例的运行时数据
 */
USTRUCT(BlueprintType)
struct FLandmarkInstanceData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector WorldLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ELandmarkType Type = ELandmarkType::Generic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLandmarkVisualConfig VisualConfig;

    // Optional: Link to an actor (e.g., City Actor). If invalid, just use WorldLocation.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TWeakObjectPtr<AActor> LinkedActor = nullptr;

    FLandmarkInstanceData() {}
};
