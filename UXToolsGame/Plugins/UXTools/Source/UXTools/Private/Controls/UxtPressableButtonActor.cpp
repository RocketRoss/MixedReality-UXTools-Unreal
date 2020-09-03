// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "Controls/UxtPressableButtonActor.h"

#include "UXTools.h"

#include "Components/AudioComponent.h"
#include "Components/TextRenderComponent.h"
#include "Controls/UxtBackPlateComponent.h"
#include "Controls/UxtButtonBrush.h"
#include "Controls/UxtPressableButtonComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Utils/UxtInternalFunctionLibrary.h"

/**
 * Pulse visuals are inherently tied to specific material properties to animate. A pulse animation occurs in 3 steps:
 *	1) The pulse position is set via the "Blob_Position" vector material parameter.
 *  2) The scalar "Blob_Pulse" material parameter is animated from 0 to 1 over the desired animation time.
 *  3) Once the pulse has finished animating it is faded out via the scaler "Blob_Fade" material parameter from 0 to 1 over the the desired
 *time.
 *
 * Note, this component also assumes the material it is animating contains two parameter variants for each step. For example "Blob_Position"
 * and "Blob_Position_2".
 */
const FName PulseInstanceNames[] = {TEXT("RightPulse"), TEXT("LeftPulse")};
const FName PulseInstanceFadeNames[] = {TEXT("RightPulseFade"), TEXT("LeftPulseFade")};
const FName PulsePositionNames[] = {TEXT("Blob_Position_2"), TEXT("Blob_Position")};
const FName PulseValueNames[] = {TEXT("Blob_Pulse_2"), TEXT("Blob_Pulse")};
const FName PulseFadeNames[] = {TEXT("Blob_Fade_2"), TEXT("Blob_Fade")};

AUxtPressableButtonActor::AUxtPressableButtonActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// Don't start ticking until the button needs to be animated.
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Apply the default label settings.
	LabelTextBrush.RelativeLocation = FVector(0, 0, -1);
	LabelTextBrush.Size = 0.5f;

	// Apply default button settings and subscriptions.
	ButtonComponent->SetPushBehavior(EUxtPushBehavior::Compress);
	ButtonComponent->OnButtonPressed.AddDynamic(this, &AUxtPressableButtonActor::OnButtonPressed);
	ButtonComponent->OnButtonReleased.AddDynamic(this, &AUxtPressableButtonActor::OnButtonReleased);
	ButtonComponent->OnBeginFocus.AddDynamic(this, &AUxtPressableButtonActor::OnBeginFocus);
	ButtonComponent->OnButtonEnabled.AddDynamic(this, &AUxtPressableButtonActor::OnButtonEnabled);
	ButtonComponent->OnButtonDisabled.AddDynamic(this, &AUxtPressableButtonActor::OnButtonDisabled);

	// Create the component hierarchy.
	BackPlatePivotComponent = CreateAndAttachComponent<USceneComponent>(TEXT("BackPlatePivot"), RootComponent);
	BackPlateMeshComponent = CreateAndAttachComponent<UUxtBackPlateComponent>(TEXT("BackPlate"), BackPlatePivotComponent);
	FrontPlatePivotComponent = CreateAndAttachComponent<USceneComponent>(TEXT("FrontPlatePivot"), RootComponent);
	FrontPlateCenterComponent = CreateAndAttachComponent<USceneComponent>(TEXT("FrontPlateCenter"), FrontPlatePivotComponent);
	FrontPlateMeshComponent = CreateAndAttachComponent<UStaticMeshComponent>(TEXT("FrontPlate"), FrontPlateCenterComponent);
	IconComponent = CreateAndAttachComponent<UTextRenderComponent>(TEXT("Icon"), FrontPlateCenterComponent);
	IconComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	IconComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	LabelComponent = CreateAndAttachComponent<UTextRenderComponent>(TEXT("Label"), FrontPlateCenterComponent);
	LabelComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LabelComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	AudioComponent = CreateAndAttachComponent<UAudioComponent>(TEXT("Audio"), RootComponent);
	AudioComponent->SetAutoActivate(false);
#if WITH_EDITORONLY_DATA
	AudioComponent->bVisualizeComponent = false; // Avoids audio icon occlusion of the button visuals in the editor.
#endif
}

void AUxtPressableButtonActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ConstructVisuals();
	ConstructIcon();
	ConstructLabel();
}

void AUxtPressableButtonActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const bool PulseComplete = AnimatePulse(DeltaTime);
	const bool FocusComplete = AnimateFocus(DeltaTime);

	if (PulseComplete && FocusComplete)
	{
		SetActorTickEnabled(false);
	}
}

void AUxtPressableButtonActor::ConstructVisuals()
{
	// Apply the back plate material and mesh if specified by the button brush.
	if (ButtonBrush.Visuals.BackPlateMaterial != nullptr)
	{
		BackPlateMeshComponent->SetBackPlateMaterial(ButtonBrush.Visuals.BackPlateMaterial);
	}

	if (ButtonBrush.Visuals.BackPlateMesh != nullptr)
	{
		BackPlateMeshComponent->SetStaticMesh(ButtonBrush.Visuals.BackPlateMesh);
	}

	const FVector Size = GetSize();

	// Swizzle the back plate size to match the content basis and leave the depth unmodified.
	BackPlateMeshComponent->SetRelativeScale3D(FVector(Size.Z, Size.Y, BackPlateMeshComponent->GetRelativeScale3D().Z));
	BackPlateMeshComponent->SetVisibility(bIsPlated);

	FrontPlateCenterComponent->SetRelativeLocation(FVector(Size.X * 0.5f, 0, 0));

	// Apply the front plate material and mesh if specified by the button brush.
	if (ButtonBrush.Visuals.FrontPlateMaterial != nullptr)
	{
		FrontPlateMeshComponent->SetMaterial(0, ButtonBrush.Visuals.FrontPlateMaterial);
	}

	if (ButtonBrush.Visuals.FrontPlateMesh != nullptr)
	{
		FrontPlateMeshComponent->SetStaticMesh(ButtonBrush.Visuals.FrontPlateMesh);
	}

	FrontPlateMeshComponent->SetRelativeScale3D(Size);
	FrontPlateMeshComponent->SetRelativeRotation(FRotator(180, 0, 0));

	// Configure the button component.
	FComponentReference Visuals;
	Visuals.PathToComponent = FrontPlatePivotComponent->GetName();
	ButtonComponent->SetVisuals(Visuals);
	ButtonComponent->SetMaxPushDistance(Size.X);
}

void ApplyTextBrushToText(UTextRenderComponent* Text, const FUxtTextBrush& TextBrush)
{
	Text->SetRelativeLocation(TextBrush.RelativeLocation);
	Text->SetRelativeRotation(TextBrush.RelativeRotation);
	Text->SetWorldSize(TextBrush.Size);
	Text->SetFont(TextBrush.Font);
	Text->SetMaterial(0, TextBrush.Material);
	Text->SetTextRenderColor(TextBrush.DefaultColor);
}

void AUxtPressableButtonActor::ConstructIcon()
{
	ApplyTextBrushToText(IconComponent, IconBrush.TextBrush);

	switch (IconBrush.ContentType)
	{
	default:
	case EUxtIconBrushContentType::None:
	{
		IconComponent->SetVisibility(false);
		IconComponent->SetText(FText::GetEmpty());
	}
	break;
	case EUxtIconBrushContentType::UnicodeCharacter:
	{
		IconComponent->SetVisibility(true);

		FString Output;
		const bool Result = UUxtInternalFunctionLibrary::HexCodePointToFString(IconBrush.Icon, Output);
		UE_CLOG(
			!Result, UXTools, Warning, TEXT("Failed to resolve hex code point '%s' on AUxtPressableButtonActor '%s'."), *IconBrush.Icon,
			*GetName());
		IconComponent->SetText(FText::FromString(Output));
	}
	break;
	case EUxtIconBrushContentType::String:
	{
		IconComponent->SetVisibility(true);
		IconComponent->SetText(FText::FromString(IconBrush.Icon));
	}
	break;
	}
}

void AUxtPressableButtonActor::ConstructLabel()
{
	ApplyTextBrushToText(LabelComponent, LabelTextBrush);

	LabelComponent->SetText(Label);
}

bool AUxtPressableButtonActor::BeginPulse(const UUxtPointerComponent* Pointer)
{
	if (IsPulsing() || (Pointer == nullptr))
	{
		return false;
	}

	PulseTimer = 0;
	PulseFadeTimer = 0;
	PrePulseMaterial = FrontPlateMeshComponent->GetMaterial(0);

	// Create a material instance based on the hand triggering the pulse.
	MaterialIndex = (Pointer->Hand == EControllerHand::Left) ? 1 : 0;
	UMaterialInterface* PulseMaterials[2] = {
		ButtonBrush.Visuals.FrontPlatePulseRightMaterial, ButtonBrush.Visuals.FrontPlatePulseLeftMaterial};
	PulseMaterialInstance =
		FrontPlateMeshComponent->CreateDynamicMaterialInstance(0, PulseMaterials[MaterialIndex], PulseInstanceNames[MaterialIndex]);

	// Set the pulse's initial location.
	const FVector PulseLocation = Pointer->GetCursorTransform().GetLocation() - FrontPlateMeshComponent->GetForwardVector();
	PulseMaterialInstance->SetVectorParameterValue(PulsePositionNames[MaterialIndex], PulseLocation);

	// Begin animating the pulse.
	SetActorTickEnabled(true);

	return true;
}

void AUxtPressableButtonActor::SetMillimeterSize(FVector Size)
{
	if (MillimeterSize != Size)
	{
		MillimeterSize = Size;
		ConstructVisuals();
	}
}

void AUxtPressableButtonActor::SetSize(FVector Size)
{
	FVector NewSize = Size * 10;

	if (MillimeterSize != NewSize)
	{
		MillimeterSize = NewSize;
		ConstructVisuals();
	}
}

void AUxtPressableButtonActor::SetIsPlated(bool IsPlated)
{
	if (bIsPlated != IsPlated)
	{
		bIsPlated = IsPlated;
		BackPlateMeshComponent->SetVisibility(IsPlated);
	}
}

void AUxtPressableButtonActor::SetIconBrush(const FUxtIconBrush& Brush)
{
	IconBrush = Brush;
	ConstructIcon();
}

void AUxtPressableButtonActor::SetLabel(const FText& NewLabel)
{
	Label = NewLabel;
	ConstructLabel();
}

void AUxtPressableButtonActor::SetLabelTextBrush(const FUxtTextBrush& Brush)
{
	LabelTextBrush = Brush;
	ConstructLabel();
}

void AUxtPressableButtonActor::SetButtonBrush(const FUxtButtonBrush& Brush)
{
	ButtonBrush = Brush;
	ConstructVisuals();
}

void AUxtPressableButtonActor::OnButtonPressed(UUxtPressableButtonComponent* Button, UUxtPointerComponent* Pointer)
{
	AudioComponent->SetSound(ButtonBrush.Audio.PressedSound);
	AudioComponent->Play();

	BeginPulse(Pointer);
}

void AUxtPressableButtonActor::OnButtonReleased(UUxtPressableButtonComponent* Button, UUxtPointerComponent* Pointer)
{
	AudioComponent->SetSound(ButtonBrush.Audio.ReleasedSound);
	AudioComponent->Play();
}

void AUxtPressableButtonActor::OnBeginFocus(UUxtPressableButtonComponent* Button, UUxtPointerComponent* Pointer, bool WasAlreadyFocused)
{
	SetActorTickEnabled(true);
}

void AUxtPressableButtonActor::OnButtonEnabled(UUxtPressableButtonComponent* Button)
{
	FrontPlateMeshComponent->SetVisibility(true);
	IconComponent->SetTextRenderColor(IconBrush.TextBrush.DefaultColor);
	LabelComponent->SetTextRenderColor(IconBrush.TextBrush.DefaultColor);
}

void AUxtPressableButtonActor::OnButtonDisabled(UUxtPressableButtonComponent* Button)
{
	FrontPlateMeshComponent->SetVisibility(false);
	IconComponent->SetTextRenderColor(IconBrush.TextBrush.DisabledColor);
	LabelComponent->SetTextRenderColor(IconBrush.TextBrush.DisabledColor);
}

bool AUxtPressableButtonActor::AnimatePulse(float DeltaTime)
{
	if (PulseTimer > 1)
	{
		if (PulseFadeTimer > 1)
		{
			// Restore back to the non-pulse state.
			FrontPlateMeshComponent->SetMaterial(0, PrePulseMaterial);
			PulseMaterialInstance = nullptr;
			PulseTimer = -1;
			PulseFadeTimer = -1;
		}
		else
		{
			// Fade out the pulse.
			PulseMaterialInstance->SetScalarParameterValue(PulseFadeNames[MaterialIndex], PulseFadeTimer);
			PulseFadeTimer += (1.f / ((ButtonBrush.Visuals.PulseFadeTime <= 0) ? 1.f : ButtonBrush.Visuals.PulseFadeTime)) * DeltaTime;

			return false;
		}
	}
	else if (PulseTimer >= 0)
	{
		// Animate the pulse.
		PulseMaterialInstance->SetScalarParameterValue(PulseValueNames[MaterialIndex], PulseTimer);
		PulseTimer += (1.f / ((ButtonBrush.Visuals.PulseTime <= 0) ? 1.f : ButtonBrush.Visuals.PulseTime)) * DeltaTime;

		if (PulseTimer > 1)
		{
			PulseFadeTimer = 0;
			PulseMaterialInstance =
				FrontPlateMeshComponent->CreateDynamicMaterialInstance(0, PrePulseMaterial, PulseInstanceFadeNames[MaterialIndex]);
			PulseMaterialInstance->SetScalarParameterValue(PulseFadeNames[MaterialIndex], PulseFadeTimer);
		}

		return false;
	}

	return true;
}

bool AUxtPressableButtonActor::AnimateFocus(float DeltaTime)
{
	const bool IsFocused = ButtonComponent->GetState() == EUxtButtonState::Focused;
	const float FocusSpeed = ButtonBrush.Visuals.IconFocusSpeed * (IsFocused ? 1 : -1);
	FocusTimer = FMath::Clamp(FocusTimer + (DeltaTime * FocusSpeed), 0.f, 1.f);
	const float CurveTime =
		(ButtonBrush.Visuals.IconFocusCurve != nullptr) ? ButtonBrush.Visuals.IconFocusCurve->GetFloatValue(FocusTimer) : FocusTimer;
	IconComponent->SetRelativeLocation(FMath::Lerp(
		IconBrush.TextBrush.RelativeLocation, IconBrush.TextBrush.RelativeLocation + (GetActorForwardVector() * (GetSize().X * -0.25f)),
		CurveTime));

	return FocusTimer == 0;
}
