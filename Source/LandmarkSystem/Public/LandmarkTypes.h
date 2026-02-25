#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassEntityHandle.h"
#include "MassCommonFragments.h"
#include "LandmarkTypes.generated.h"

// --- Landmark SubType Definitions (Global IDs 0-99) ---
// Reserved 10-15 for Cities to avoid conflict with standard units (0-9)
#define LandmarkSubType_City    10
#define LandmarkSubType_City1   11
#define LandmarkSubType_City2   12
#define LandmarkSubType_City3   13
#define LandmarkSubType_City4   14
#define LandmarkSubType_City5   15

USTRUCT()
struct LANDMARKSYSTEM_API FLandmarkFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FString LandmarkID;

    UPROPERTY()
    int32 VictoryPoints = 0;

    UPROPERTY()
    FVector VisualOffset = FVector::ZeroVector;
};

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

 	/* Minimum Camera Height (Z) at which this landmark is visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	float MinVisibleHeight = 0.0f;

	/* Maximum Camera Height (Z) at which this landmark is visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	float MaxVisibleHeight = 100000.0f;

	/* Base Scale of the text/icon. (Constant, does not change with zoom) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	float BaseScale = 1.0f;

    /* Text Color */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
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
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double X = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double Y = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double ZMin = 0.0; // MinVisibleHeight

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    double ZMax = 100000.0; // MaxVisibleHeight

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Type = TEXT("Generic");

    // Helper ID for internal tracking (can default to Name)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TWeakObjectPtr<AActor> LinkedActor = nullptr;

    // Victory Points or Value (e.g. 5 points)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Value = 0;

    // Visual Offset for the label (e.g. to raise it above the city mesh)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector VisualOffset = FVector::ZeroVector;

    // Mass Entity Handle (not reflected, runtime only)
    FMassEntityHandle EntityHandle;

    // Visual Template for Mass Representative
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftClassPtr<AActor> RepresentationClass;

    FLandmarkInstanceData() {}
    
    FVector GetLocation() const { return FVector(X, Y, 0.0); }
};
