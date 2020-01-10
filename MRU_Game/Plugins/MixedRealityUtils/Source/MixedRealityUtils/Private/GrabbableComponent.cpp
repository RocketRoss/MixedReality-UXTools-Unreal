// Fill out your copyright notice in the Description page of Project Settings.

#include "GrabbableComponent.h"
#include "TouchPointer.h"


FVector UGrabPointerDataFunctionLibrary::GetGrabLocation(const FTransform &Transform, const FGrabPointerData &PointerData)
{
	return Transform.TransformPosition(PointerData.LocalGrabPoint.GetLocation());
}

FRotator UGrabPointerDataFunctionLibrary::GetGrabRotation(const FTransform &Transform, const FGrabPointerData &PointerData)
{
	return Transform.TransformRotation(PointerData.LocalGrabPoint.GetRotation()).Rotator();
}

FTransform UGrabPointerDataFunctionLibrary::GetGrabTransform(const FTransform &Transform, const FGrabPointerData &PointerData)
{
	return PointerData.LocalGrabPoint * Transform;
}

FVector UGrabPointerDataFunctionLibrary::GetTargetLocation(const FGrabPointerData &PointerData)
{
	if (ensure(PointerData.Pointer != nullptr))
	{
		return PointerData.Pointer->GetComponentLocation();
	}
	return FVector::ZeroVector;
}

FRotator UGrabPointerDataFunctionLibrary::GetTargetRotation(const FGrabPointerData &PointerData)
{
	if (ensure(PointerData.Pointer != nullptr))
	{
		return PointerData.Pointer->GetComponentRotation();
	}
	return FRotator::ZeroRotator;
}

FTransform UGrabPointerDataFunctionLibrary::GetTargetTransform(const FGrabPointerData &PointerData)
{
	if (ensure(PointerData.Pointer != nullptr))
	{
		return PointerData.Pointer->GetComponentTransform();
	}
	return FTransform::Identity;
}

FVector UGrabPointerDataFunctionLibrary::GetLocationOffset(const FTransform &Transform, const FGrabPointerData &PointerData)
{
	return UGrabPointerDataFunctionLibrary::GetTargetLocation(PointerData) - UGrabPointerDataFunctionLibrary::GetGrabLocation(Transform, PointerData);
}

FRotator UGrabPointerDataFunctionLibrary::GetRotationOffset(const FTransform &Transform, const FGrabPointerData &PointerData)
{
	return (FQuat(UGrabPointerDataFunctionLibrary::GetTargetRotation(PointerData)) * FQuat(UGrabPointerDataFunctionLibrary::GetGrabRotation(Transform, PointerData).GetInverse())).Rotator();
}


UGrabbableComponent::UGrabbableComponent()
{

}

const TArray<FGrabPointerData> &UGrabbableComponent::GetGrabPointers() const
{
	return GrabPointers;
}

FVector UGrabbableComponent::GetGrabPointCentroid(const FTransform &Transform) const
{
	FVector centroid = FVector::ZeroVector;
	for (const FGrabPointerData &data : GrabPointers)
	{
		centroid += UGrabPointerDataFunctionLibrary::GetGrabLocation(Transform, data);
	}
	centroid /= FMath::Max(GrabPointers.Num(), 1);
	return centroid;
}

FVector UGrabbableComponent::GetTargetCentroid() const
{
	FVector centroid = FVector::ZeroVector;
	for (const FGrabPointerData &data : GrabPointers)
	{
		if (ensure(data.Pointer != nullptr))
		{
			centroid += UGrabPointerDataFunctionLibrary::GetTargetLocation(data);
		}
	}
	centroid /= FMath::Max(GrabPointers.Num(), 1);
	return centroid;
}

bool UGrabbableComponent::FindGrabPointerInternal(UTouchPointer *Pointer, FGrabPointerData const *&OutData, int &OutIndex) const
{
	for (int i = 0; i < GrabPointers.Num(); ++i)
	{
		const FGrabPointerData &data = GrabPointers[i];
		if (data.Pointer == Pointer)
		{
			OutData = &data;
			OutIndex = i;
			return true;
		}
	}

	OutData = nullptr;
	OutIndex = -1;
	return false;
}

void UGrabbableComponent::FindGrabPointer(UTouchPointer *Pointer, bool &Success, FGrabPointerData &PointerData, int &Index) const
{
	FGrabPointerData const *pData;
	Success = FindGrabPointerInternal(Pointer, pData, Index);
	if (Success)
	{
		PointerData = *pData;
	}
}

void UGrabbableComponent::GetPrimaryGrabPointer(bool &Valid, FGrabPointerData &PointerData) const
{
	if (GrabPointers.Num() >= 1)
	{
		PointerData = GrabPointers[0];
		Valid = true;
	}
	else
	{
		Valid = false;
	}
}

void UGrabbableComponent::GetSecondaryGrabPointer(bool &Valid, FGrabPointerData &PointerData) const
{
	if (GrabPointers.Num() >= 2)
	{
		PointerData = GrabPointers[1];
		Valid = true;
	}
	else
	{
		Valid = false;
	}
}

bool UGrabbableComponent::GetTickOnlyWhileGrabbed() const
{
	return bTickOnlyWhileGrabbed;
}

void UGrabbableComponent::SetTickOnlyWhileGrabbed(bool bEnable)
{
	bTickOnlyWhileGrabbed = bEnable;

	if (bEnable)
	{
		UpdateComponentTickEnabled();
	}
	else
	{
		PrimaryComponentTick.SetTickFunctionEnable(true);
	}
}

void UGrabbableComponent::UpdateComponentTickEnabled()
{
	if (bTickOnlyWhileGrabbed)
	{
		PrimaryComponentTick.SetTickFunctionEnable(GrabPointers.Num() > 0);
	}
}

void UGrabbableComponent::GraspStarted_Implementation(UTouchPointer* Pointer)
{
	Super::GraspStarted_Implementation(Pointer);

	FGrabPointerData data;
	data.Pointer = Pointer;
	data.StartTime = GetWorld()->GetTimeSeconds();
	data.LocalGrabPoint = Pointer->GetComponentTransform() * GetComponentTransform().Inverse();

	GrabPointers.Add(data);

	// Lock the grabbing pointer so we remain the hovered target as it moves.
	Pointer->SetHoverLocked(true);

	OnBeginGrab.Broadcast(this, data);

	UpdateComponentTickEnabled();
}

void UGrabbableComponent::ResetLocalGrabPoint(FGrabPointerData &PointerData)
{
	PointerData.LocalGrabPoint = PointerData.Pointer->GetComponentTransform() * GetComponentTransform().Inverse();
}

void UGrabbableComponent::GraspEnded_Implementation(UTouchPointer* Pointer)
{
	int numRemoved = GrabPointers.RemoveAll([this, Pointer](const FGrabPointerData &data)
	{
		if (data.Pointer == Pointer)
		{
			Pointer->SetHoverLocked(false);
			OnEndGrab.Broadcast(this, data);
			return true;
		}
		return false;
	});

	UpdateComponentTickEnabled();

	Super::GraspEnded_Implementation(Pointer);
}
