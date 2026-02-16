#include "LandmarkComponent.h"
#include "LandmarkSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"

ULandmarkComponent::ULandmarkComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
    
    	// Default Configs
    MinVisibleHeight = 0.0f;
    MaxVisibleHeight = 100000.0f;
    
    // Default config
    VisualConfig.BaseScale = 1.0f;
}

void ULandmarkComponent::OnRegister()
{
    Super::OnRegister();
#if WITH_EDITOR
    if (!GetWorld() || !GetWorld()->IsGameWorld())
    {
        EnsureEditorComponents();
        UpdateEditorVisuals();
    }
    else
    {
        // In Game World, we don't need visuals
        // They are transient, so they might not even exist, which is good.
    }
#endif
}

void ULandmarkComponent::BeginPlay()
{
	Super::BeginPlay();

    // 1. Register Data
    if (ULandmarkSubsystem* Subsystem = GetWorld()->GetSubsystem<ULandmarkSubsystem>())
    {
        FLandmarkInstanceData Data;
        Data.ID = ID.IsEmpty() ? GetName() : ID;
        Data.Name = DisplayName.ToString();
        if (Data.Name.IsEmpty()) Data.Name = GetName();
        
        FVector Loc = GetComponentLocation();
        Data.X = Loc.X;
        Data.Y = Loc.Y;
        
        Data.Type = Type.ToString(); 
        
        Data.ZMin = MinVisibleHeight;
        Data.ZMax = MaxVisibleHeight;
        
        // This component is usually attached to a specific actor (like a City)
        Data.LinkedActor = GetOwner(); 

        Subsystem->RegisterLandmark(Data);
    }

    // 2. Zero Overhead Optimization
    // If it's a static landmark, we don't need this component anymore.
    // However, DestroyComponent might be too aggressive if other logic depends on it.
    // Safe bet: Disable Tick (already done) and simple exist.
    // If user wants Extreme Optimization, we could DestroyComponent().
    // Let's keep it alive but dormant for now (SceneComponent is cheap).
    // DestroyComponent(); // Uncomment if you want them gone.
}

#if WITH_EDITOR
void ULandmarkComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    UpdateEditorVisuals();
}

void ULandmarkComponent::EnsureEditorComponents()
{
    // Only Create visuals in Editor World
    if (GetWorld() && GetWorld()->IsGameWorld()) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    if (!SpriteComponent)
    {
        SpriteComponent = NewObject<UBillboardComponent>(Owner, NAME_None, RF_Transient);
        SpriteComponent->SetupAttachment(this);
        SpriteComponent->RegisterComponent();
        SpriteComponent->SetHiddenInGame(true);
        // Load default icon?
    }

    if (!TextComponent)
    {
        TextComponent = NewObject<UTextRenderComponent>(Owner, NAME_None, RF_Transient);
        TextComponent->SetupAttachment(this);
        TextComponent->RegisterComponent();
        TextComponent->SetHiddenInGame(true);
        TextComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
        TextComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
        TextComponent->SetRelativeLocation(FVector(0, 0, 50));
    }
}

void ULandmarkComponent::UpdateEditorVisuals()
{
    if (TextComponent)
    {
        FString TextToDisplay = DisplayName.ToString();
        if (TextToDisplay.IsEmpty()) TextToDisplay = ID.IsEmpty() ? TEXT("Landmark") : ID;
        
        TextComponent->SetText(FText::FromString(TextToDisplay));
        TextComponent->SetTextRenderColor(VisualConfig.Color.ToFColor(true));
        TextComponent->SetWorldSize(100.0f * VisualConfig.BaseScale);
    }
}
#endif
