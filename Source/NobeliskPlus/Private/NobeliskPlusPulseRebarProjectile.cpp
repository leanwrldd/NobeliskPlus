#include "NobeliskPlusPulseRebarProjectile.h"

#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
#include "NobeliskPlus.h"

// Defaults mirror the RadialForceComponent that BP_Rebar_Explosive carries, so the numbers
// stay recognisable; the module overwrites them from the mod configuration on startup.
float ANobeliskPlusPulseRebarProjectile::PulseRadius = 500.0f;
float ANobeliskPlusPulseRebarProjectile::PulseLaunchVelocity = 1200.0f;
float ANobeliskPlusPulseRebarProjectile::PulsePhysicsImpulse = 120000.0f;

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

	const float radius = PulseRadius;
	if (radius <= 0.0f)
		return;

	bPulseApplied = true;

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

			// LaunchCharacter takes a VELOCITY (cm/s), not an impulse - feeding it a
			// RadialForceComponent-style impulse number would be nonsense.
			character->LaunchCharacter(direction * PulseLaunchVelocity * falloff, true, true);
			++charactersPushed;
			continue;
		}

		UPrimitiveComponent* component = overlap.GetComponent();
		if (component != nullptr && component->IsSimulatingPhysics())
		{
			component->AddRadialImpulse(origin, radius, PulsePhysicsImpulse, RIF_Linear, true);
			++bodiesPushed;
		}
	}

	UE_LOG(LogNobeliskPlus, Verbose,
		TEXT("Pulse Rebar shockwave at %s: radius %.0f, launch velocity %.0f, physics impulse %.0f -> pushed %d character(s), %d physics body/bodies."),
		*origin.ToCompactString(), radius, PulseLaunchVelocity, PulsePhysicsImpulse, charactersPushed, bodiesPushed);
}
