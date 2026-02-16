#include "LandmarkMapLabelProxy.h"
#include "LandmarkSubsystem.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

ALandmarkMapLabelProxy::ALandmarkMapLabelProxy()
{
	PrimaryActorTick.bCanEverTick = false;
	// bIsEditorOnlyActor = true; // REMOVED: Needs to exist at runtime to register itself (Dynamic)
    bIsEditorOnlyActor = false; 

	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneComponent;

	Icon = CreateDefaultSubobject<UBillboardComponent>(TEXT("Icon"));
	Icon->SetupAttachment(RootComponent);
    Icon->SetHiddenInGame(true); // Hide in game
	
	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LabelText"));
	LabelText->SetupAttachment(RootComponent);
	LabelText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LabelText->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	LabelText->SetWorldSize(100.0f);
	LabelText->SetRelativeLocation(FVector(0, 0, 100));
    LabelText->SetHiddenInGame(true); // Hide in game (system draws HUD text)
}

void ALandmarkMapLabelProxy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisuals();
}

void ALandmarkMapLabelProxy::BeginPlay()
{
	Super::BeginPlay();

	// Register Self
	if (ULandmarkSubsystem* Subsystem = GetWorld()->GetSubsystem<ULandmarkSubsystem>())
	{
		FLandmarkInstanceData Data;
		Data.ID = ID.IsEmpty() ? GetName() : ID;
		Data.DisplayName = DisplayName.IsEmpty() ? FText::FromString(GetName()) : DisplayName;
		Data.WorldLocation = GetActorLocation();
		Data.Type = Type;
		Data.VisualConfig = VisualConfig;
		// No linked actor needed for static proxy, or link self if we want it to move
		Data.LinkedActor = nullptr; 

		Subsystem->RegisterLandmark(Data);
	}
}

#if WITH_EDITOR
void ALandmarkMapLabelProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateVisuals();
}

void ALandmarkMapLabelProxy::SnapToGround()
{
	FVector Start = GetActorLocation();
	FVector End = Start - FVector(0, 0, 100000.0f); // Trace down 1km+
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

    // Trace complex to hit landscape properly
    Params.bTraceComplex = true;

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		SetActorLocation(Hit.ImpactPoint);
#if WITH_EDITOR
        // Ensure Editor knows it moved
        Modify(); 
#endif
	}
}
#endif

void ALandmarkMapLabelProxy::UpdateVisuals()
{
	if (LabelText)
	{
		FString TextToDisplay = DisplayName.ToString();
		if (TextToDisplay.IsEmpty())
		{
			TextToDisplay = ID.IsEmpty() ? TEXT("Landmark") : ID;
		}
		LabelText->SetText(FText::FromString(TextToDisplay));
		LabelText->SetTextRenderColor(VisualConfig.Color.ToFColor(true));
		LabelText->SetWorldSize(100.0f * VisualConfig.BaseScale);
	}
}
