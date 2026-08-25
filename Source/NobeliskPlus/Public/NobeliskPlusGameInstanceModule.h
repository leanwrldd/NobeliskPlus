#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "NobeliskPlusGameInstanceModule.generated.h"

class UConfigPropertyFloat;
class UConfigPropertySection;
class URadialForceComponent;
class UFGProjectileMovementComponent;

/// One shockwave-producing projectile whose RadialForceComponent is amplified by a
/// trio of config-driven multipliers ("Radius/Impulse/ForceMultiplier" under the given
/// config subsection). Used for both the Pulse Nobelisk and the Pulse Rebar.
USTRUCT()
struct FNobeliskPlusShockwaveTarget
{
	GENERATED_BODY()

	/// Path to the projectile Blueprint, e.g. "/Game/.../BP_NobeliskShockwave.BP_NobeliskShockwave_C".
	FString ProjectilePath;

	/// Key of this target's subsection under the configuration's root section (see
	/// UNobeliskPlusConfiguration), e.g. "Nobelisk" or "Rebar".
	FString ConfigSectionName;

	/// True if this projectile's RadialForceComponent needs its ObjectTypesToAffect
	/// populated (Pawn, PhysicsBody, ...) because its source Blueprint's own is empty -
	/// e.g. BP_Rebar_Explosive's component is configured for minor debris knock only and
	/// was never set up to affect players. BP_NobeliskShockwave's is already correct.
	bool bEnsurePlayerCanBePushed = false;

	/// True if this target drives ANobeliskPlusPulseRebarProjectile, whose push is applied in
	/// C++ rather than by the projectile Blueprint - see that class for why.
	bool bDrivesNativePulse = false;

	UPROPERTY()
	TObjectPtr<URadialForceComponent> RadialForce;

	UPROPERTY()
	TObjectPtr<UConfigPropertyFloat> RadiusMultiplierProperty;

	UPROPERTY()
	TObjectPtr<UConfigPropertyFloat> ImpulseMultiplierProperty;

	UPROPERTY()
	TObjectPtr<UConfigPropertyFloat> ForceMultiplierProperty;

	/// Only set for targets that drive a gun-fired projectile (the Pulse Rebar). The Pulse
	/// Nobelisk is thrown, so a muzzle-velocity setting would be meaningless for it.
	UPROPERTY()
	TObjectPtr<UConfigPropertyFloat> SpeedMultiplierProperty;

	UPROPERTY()
	TObjectPtr<UFGProjectileMovementComponent> ProjectileMovement;

	float BaseInitialSpeed = 0.0f;
	float BaseMaxSpeed = 0.0f;

	/// The shockwave's un-amplified values, captured once so config changes multiply
	/// from the original baseline instead of compounding on top of the last edit.
	float BaseRadius = 0.0f;
	float BaseImpulseStrength = 0.0f;
	float BaseForceStrength = 0.0f;
};

/// Patches the Pulse Nobelisk's and Pulse Rebar's RadialForceComponent so their
/// shockwaves are bigger and stronger, without touching damage (a separate
/// FGDamageType system, and the Pulse Rebar deals none to begin with). The
/// multipliers are exposed as an in-game mod configuration (UNobeliskPlusConfiguration)
/// so players can tune them from the mod settings menu, live, without restarting.
UCLASS()
class NOBELISKPLUS_API UNobeliskPlusGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UNobeliskPlusGameInstanceModule();

	// UGameInstanceModule
	virtual void DispatchLifecycleEvent(ELifecyclePhase phase) override;

private:
	UFUNCTION()
	void OnShockwaveConfigChanged();

	void SetUpShockwaveTarget(FNobeliskPlusShockwaveTarget& target, UConfigPropertySection* configRoot);
	void ApplyShockwaveTarget(const FNobeliskPlusShockwaveTarget& target) const;
	void RegisterPulseRebarAsRebarGunAmmo() const;
	void ClearPulseRebarAmmoDamage() const;

	UPROPERTY()
	FNobeliskPlusShockwaveTarget NobeliskTarget;

	UPROPERTY()
	FNobeliskPlusShockwaveTarget RebarTarget;

	/// Keeps the patched CDO subobjects referenced so they aren't garbage collected.
	UPROPERTY()
	TArray<TObjectPtr<UObject>> CDOEdits;
};
