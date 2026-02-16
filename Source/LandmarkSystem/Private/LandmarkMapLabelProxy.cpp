#include "LandmarkMapLabelProxy.h"

ALandmarkMapLabelProxy::ALandmarkMapLabelProxy()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true; // CRITICAL: This ensures it is STRIPPED from packaged games.

	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneComponent;

	Icon = CreateDefaultSubobject<UBillboardComponent>(TEXT("Icon"));
	Icon->SetupAttachment(RootComponent);
	
	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LabelText"));
	LabelText->SetupAttachment(RootComponent);
	LabelText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LabelText->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	LabelText->SetWorldSize(100.0f);
	LabelText->SetRelativeLocation(FVector(0, 0, 100));
}

void ALandmarkMapLabelProxy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisuals();
}

#if WITH_EDITOR
void ALandmarkMapLabelProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateVisuals();
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
