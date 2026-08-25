#pragma once

#include "CoreMinimal.h"
#include "FGProjectile.h"
class UParticleSystem;
class UNiagaraSystem;
class UStaticMeshComponent;
#include "NobeliskPlusPulseRebarProjectile.generated.h"

/// Projectile for the Pulse Rebar.
///
/// Why this exists at all: BP_Rebar_Pulse was made by duplicating the base game's
/// BP_Rebar_Explosive, but the copy of that asset shipped in the modding starter project is
/// COOKED - its Blueprint event graph has been stripped, so the duplicate came out with an
/// empty graph. Whatever logic vanilla runs on impact to produce the shockwave (the
/// RadialForceComponent sitting on the Blueprint is only configuration; something has to
/// actually fire it, and a player character's capsule doesn't respond to physics impulses
/// anyway - it needs LaunchCharacter) was silently lost in the duplication and cannot be
/// recovered from the stripped asset. So the push is reimplemented here in C++ instead,
/// where it is fully under our control and doesn't depend on any Blueprint graph content.
///
/// BP_Rebar_Pulse must be REPARENTED to this class for any of it to run.
UCLASS()
class NOBELISKPLUS_API ANobeliskPlusPulseRebarProjectile : public AFGProjectile
{
	GENERATED_BODY()

public:
	ANobeliskPlusPulseRebarProjectile();

	/// The visible rebar in flight. Reparenting BP_Rebar_Pulse onto this class detached it
	/// from BP_RebarProjectile (the shared Blueprint base all vanilla rebar rounds derive
	/// from), which is where the mesh component lived - so the round rendered as nothing at
	/// all. Recreated natively here instead of relying on the Blueprint carrying one.
	UPROPERTY(VisibleAnywhere, Category = "Pulse Rebar")
	TObjectPtr<UStaticMeshComponent> RebarMesh;

	/// Live values, pushed here by UNobeliskPlusGameInstanceModule whenever the mod's
	/// configuration changes. Static because the projectile is spawned per shot and has no
	/// convenient route back to the module.
	static float PulseRadius;
	static float PulseLaunchVelocity;
	static float PulsePhysicsImpulse;

	/// Seconds of stun applied to creatures so their AI movement doesn't immediately cancel
	/// the launch. Does not apply to the player.
	static float PulseStunDuration;

	/// Discovered once at startup by scanning BP_NobeliskShockwave_C's class hierarchy for
	/// any UParticleSystem/UNiagaraSystem asset reference - see
	/// FindImpactEffect in the .cpp. Both null if nothing was found, in which case no VFX is
	/// spawned (silently - this is a cosmetic best-effort, not something that should ever
	/// crash or block the actual push). At most one of the two is ever set.
	static UParticleSystem* ImpactEffect;
	static UNiagaraSystem* ImpactNiagaraEffect;

	// AFGProjectile
	virtual void OnImpact_Native(const FHitResult& hitResult) override;

protected:
	virtual void OnExplode_Implementation() override;

private:
	/// Pushes characters (via LaunchCharacter) and physics bodies (via AddRadialImpulse)
	/// away from the given point, with linear falloff over PulseRadius.
	void ApplyPulse(const FVector& origin);

	/// A rebar can both impact and then explode; only one push per projectile.
	bool bPulseApplied = false;
};
