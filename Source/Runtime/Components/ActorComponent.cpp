#include "EnginePCH.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

UActorComponent::~UActorComponent()
{
	assert(!bRegistered && "Delete Without Unregister");
	if (Owner)
	{
		Owner->RemoveOwnedComponent(this);
	}
}

void UActorComponent::SetActive(bool bNewActive)
{
	bIsActive = bNewActive;
}

void UActorComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();
	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* OwnerObj = Owner;
		Record << SA_VALUE("Owner", OwnerObj);
		Owner = static_cast<AActor*>(OwnerObj);
	}
}

void UActorComponent::RegisterComponent()
{
	AActor* MyOwner = GetOwner();
	UWorld* MyOwnerWorld = (MyOwner ? MyOwner->GetWorld() : nullptr);
	if (MyOwnerWorld)
	{
		RegisterComponentWithWorld(MyOwnerWorld);
	}
}

void UActorComponent::RegisterComponentWithWorld(UWorld* InWorld)
{
	if (IsRegistered()) { HTR_LOG(Info, "RegisterComponentWithWorld: {} Already registered. Aborting.", GetClass()->Name); return; }
	if (!IsValid(InWorld)) { return; }

	WorldPrivate = InWorld;
	ExecuteRegisterEvents();
}

void UActorComponent::UnregisterComponent()
{
	if (!IsRegistered()) return;
	ExecuteUnregisterEvents();
	WorldPrivate = nullptr;
}

void UActorComponent::OnRegister()
{
	bRegistered = true;
}


void UActorComponent::OnUnregister()
{
	bRegistered = false;
}

void UActorComponent::CreateRenderState()
{
	bRenderStateCreated = true;
}

void UActorComponent::DestroyRenderState()
{
	bRenderStateCreated = false;
}

void UActorComponent::ExecuteRegisterEvents()
{
	if (!bRegistered)
	{
		OnRegister();
		assert(bRegistered && "Forgot to Call OnRegister: Super ");
	}
	if (!bRenderStateCreated && ShouldCreateRenderState())
	{
		CreateRenderState();
		assert(bRenderStateCreated && "Forgot to Call CreateRenderState: Super ");
	}
}

void UActorComponent::ExecuteUnregisterEvents()
{
	if (bRenderStateCreated)
	{
		DestroyRenderState();
	}
	if (bRegistered)
	{
		OnUnregister();
	}
}
