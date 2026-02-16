#include "LandmarkPathGenerator.h"
#include "LandmarkSubsystem.h"

ALandmarkPathGenerator::ALandmarkPathGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	SplinePath = CreateDefaultSubobject<USplineComponent>(TEXT("SplinePath"));
	RootComponent = SplinePath;
}

void ALandmarkPathGenerator::BeginPlay()
{
	Super::BeginPlay();
	GenerateLandmarks();
}

void ALandmarkPathGenerator::GenerateLandmarks()
{
	ULandmarkSubsystem* Subsystem = GetWorld()->GetSubsystem<ULandmarkSubsystem>();
	if (!Subsystem || !SplinePath)
	{
		return;
	}

	float SplineLength = SplinePath->GetSplineLength();
	int32 NumPoints = FMath::FloorToInt(SplineLength / FMath::Max(Spacing, 100.0f));

	for (int32 i = 0; i < NumPoints; ++i)
	{
		float Distance = i * Spacing;
		// Offset slightly so it's not exactly at the start
		if (i == 0) Distance += Spacing * 0.5f; 

		FVector Location = SplinePath->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		
		FLandmarkInstanceData Data;
		Data.ID = FString::Printf(TEXT("%s_%d"), *GetName(), i);
		Data.Name = BaseDisplayName.ToString();
		Data.X = Location.X;
        Data.Y = Location.Y;
		if (const UEnum* EnumPtr = StaticEnum<ELandmarkType>())
        {
            Data.Type = EnumPtr->GetNameStringByValue((int64)Type);
        }
        else
        {
            Data.Type = TEXT("Generic");
        }
        Data.ZMin = MinVisibleHeight;
        Data.ZMax = MaxVisibleHeight;
		// Link to self? No, these act as independent static points for now.
		Data.LinkedActor = nullptr; 

		Subsystem->RegisterLandmark(Data);
	}
}
