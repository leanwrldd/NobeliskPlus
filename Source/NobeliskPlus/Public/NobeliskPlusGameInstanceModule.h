#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "NobeliskPlusGameInstanceModule.generated.h"

class UConfigPropertyFloat;
class UConfigPropertySection;
class URadialForceComponent;

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

	UPROPERTY()
	TObjectPtr<URadialForceComponent> RadialForce;

	UPROPERTY()
	TObjectPtr<UConfigPropertyFloat> RadiusMultiplierProperty;

	UPROPERTY()
	TObjectPtr<UConfigPropertyFloat> ImpulseMultiplierProperty;

	UPROPERTY()
	TObjectPtr<UConfigPropertyFloat> ForceMultiplierProperty;

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

	UPROPERTY()
	FNobeliskPlusShockwaveTarget NobeliskTarget;

	UPROPERTY()
	FNobeliskPlusShockwaveTarget RebarTarget;

	/// Keeps the patched CDO subobjects referenced so they aren't garbage collected.
	UPROPERTY()
	TArray<TObjectPtr<UObject>> CDOEdits;
};
