#include "NobeliskPlusPulseRebarProjectile.h"

#include "Engine/OverlapResult.h"
#include "Components/StaticMeshComponent.h"
#include "Creature/FGCreature.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "NobeliskPlus.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"

// Defaults mirror the RadialForceComponent that BP_Rebar_Explosive carries, so the numbers
// stay recognisable; the module overwrites them from the mod configuration on startup.
float ANobeliskPlusPulseRebarProjectile::PulseRadius = 500.0f;
float ANobeliskPlusPulseRebarProjectile::PulseLaunchVelocity = 1200.0f;
float ANobeliskPlusPulseRebarProjectile::PulsePhysicsImpulse = 120000.0f;
float ANobeliskPlusPulseRebarProjectile::PulseStunDuration = 1.0f;
UParticleSystem* ANobeliskPlusPulseRebarProjectile::ImpactEffect = nullptr;
UNiagaraSystem* ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect = nullptr;

ANobeliskPlusPulseRebarProjectile::ANobeliskPlusPulseRebarProjectile()
{
	RebarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RebarMesh"));
	// Purely visual - the projectile already has its own collision sphere from AFGProjectile,
	// and a second colliding body would make the round hit things twice.
	RebarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RebarMesh->SetGenerateOverlapEvents(false);
	if (RootComponent != nullptr)
	{
		RebarMesh->SetupAttachment(RootComponent);
	}
	else
	{
		RootComponent = RebarMesh;
	}
}

void ANobeliskPlusPulseRebarProjectile::OnImpact_Native(const FHitResult& hitResult)
{
	ApplyPulse(hitResult.ImpactPoint.IsZero() ? GetActorLocation() : FVector(hitResult.ImpactPoint));

	Super::OnImpact_Native(hitResult);
}

void ANobeliskPlusPulseRebarProjectile::OnExplode_Implementation()
{
	// Covers the case where the round sticks and detonates later rather than pushing on the
	// initial hit; bPulseApplied keeps it to one push either way.
	ApplyPulse(GetActorLocation());

	Super::OnExplode_Implementation();
}

void ANobeliskPlusPulseRebarProjectile::ApplyPulse(const FVector& origin)
{
	if (bPulseApplied)
		return;

	UWorld* world = GetWorld();
	if (world == nullptr || !HasAuthority())
		return;
	// Single-player only for now, like the rest of this mod: HasAuthority() gates this whole
	// function to the server, and SpawnEmitterAtLocation here has no client replication, so
	// on a dedicated server or a non-host client the effect would not be visible.

	const float radius = PulseRadius;
	if (radius <= 0.0f)
		return;

	bPulseApplied = true;

	if (ImpactEffect != nullptr)
	{
		UGameplayStatics::SpawnEmitterAtLocation(world, ImpactEffect, origin);
	}
	if (ImpactNiagaraEffect != nullptr)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(world, ImpactNiagaraEffect, origin);
	}

	FCollisionObjectQueryParams objectParams;
	objectParams.AddObjectTypesToQuery(ECC_Pawn);
	objectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	objectParams.AddObjectTypesToQuery(ECC_Vehicle);
	objectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	objectParams.AddObjectTypesToQuery(ECC_Destructible);

	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(NobeliskPlusPulse), false, this);
	queryParams.AddIgnoredActor(this);

	TArray<FOverlapResult> overlaps;
	world->OverlapMultiByObjectType(overlaps, origin, FQuat::Identity, objectParams,
		FCollisionShape::MakeSphere(radius), queryParams);

	TSet<AActor*> pushedActors;
	int32 charactersPushed = 0;
	int32 bodiesPushed = 0;

	for (const FOverlapResult& overlap : overlaps)
	{
		AActor* actor = overlap.OverlapObjectHandle.FetchActor<AActor>();
		if (actor == nullptr || actor == this)
			continue;

		if (ACharacter* character = Cast<ACharacter>(actor))
		{
			if (pushedActors.Contains(actor))
				continue;
			pushedActors.Add(actor);

			const FVector delta = character->GetActorLocation() - origin;
			const float distance = delta.Size();
			const float falloff = FMath::Clamp(1.0f - distance / radius, 0.0f, 1.0f);
			if (falloff <= 0.0f)
				continue;

			// Bias upwards so the player is launched off the ground rather than scraped
			// along it - a purely horizontal velocity mostly gets eaten by ground friction.
			FVector direction = distance > KINDA_SMALL_NUMBER ? delta / distance : FVector::UpVector;
			direction = (direction + FVector(0.0f, 0.0f, 0.75f)).GetSafeNormal();

			// Creatures are AI-driven: UFGCreatureMovementComponent re-drives velocity from
			// pathfinding every tick, so a launch alone gets overwritten almost immediately
			// and they appear unaffected. Stunning them briefly suspends that so the launch
			// actually carries.
			if (AFGCreature* creature = Cast<AFGCreature>(character))
			{
				if (creature->CanBeStunned())
				{
					creature->BeginStun(PulseStunDuration);
				}
			}

			// LaunchCharacter takes a VELOCITY (cm/s), not an impulse - feeding it a
			// RadialForceComponent-style impulse number would be nonsense.
			character->LaunchCharacter(direction * PulseLaunchVelocity * falloff, true, true);
			++charactersPushed;
			UE_LOG(LogNobeliskPlus, Log, TEXT("Pulse pushed character %s (%s) at %.0fcm, falloff %.2f"),
				*character->GetName(), *character->GetClass()->GetName(), distance, falloff);
			continue;
		}

		UPrimitiveComponent* component = overlap.GetComponent();
		if (component != nullptr && component->IsSimulatingPhysics())
		{
			component->AddRadialImpulse(origin, radius, PulsePhysicsImpulse, RIF_Linear, true);
			++bodiesPushed;
		}
	}

	UE_LOG(LogNobeliskPlus, Log,
		TEXT("Pulse Rebar shockwave at %s: radius %.0f, launch velocity %.0f, physics impulse %.0f -> pushed %d character(s), %d physics body/bodies."),
		*origin.ToCompactString(), radius, PulseLaunchVelocity, PulsePhysicsImpulse, charactersPushed, bodiesPushed);
}
