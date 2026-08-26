#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "NobeliskPlusGameInstanceModule.generated.h"

class UConfigPropertyBool;
class UConfigPropertyFloat;
class UConfigPropertyInteger;
class UConfigPropertySection;
class URadialForceComponent;
class UFGProjectileMovementComponent;
class UFGAmmoType;
class AFGWeapon;

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

/// Path to an ammo descriptor Blueprint plus the config field key that should override its
/// magazine capacity, e.g. {"/Game/.../Desc_NobeliskGas.Desc_NobeliskGas_C", "GasCapacity"}.
/// Plain (non-UHT) struct: only ever used as a compile-time table, never stored on a UObject.
struct FNobeliskDescriptorInfo
{
	const TCHAR* Path;
	const TCHAR* ConfigPropertyName;
};

/// One ammo descriptor whose magazine capacity (mMagazineSize) can be overridden by a
/// per-type config field, falling back to its capacity group's shared general capacity,
/// falling back to its own vanilla default. See UNobeliskPlusGameInstanceModule::ApplyCapacityTarget.
USTRUCT()
struct FNobeliskPlusAmmoCapacityTarget
{
	GENERATED_BODY()

	/// Path to the ammo descriptor Blueprint, e.g. "/Game/.../Desc_NobeliskGas.Desc_NobeliskGas_C".
	FString DescriptorPath;

	/// Key of this target's field under the owning capacity group's config subsection,
	/// e.g. "GasCapacity".
	FString ConfigPropertyName;

	UPROPERTY()
	TObjectPtr<UFGAmmoType> AmmoCdo;

	UPROPERTY()
	TObjectPtr<UConfigPropertyInteger> SpecificCapacityProperty;

	/// This type's un-overridden magazine size, captured once so turning an override back
	/// off (setting it to 0) restores the original value rather than whatever was last applied.
	int32 BaseMagazineSize = 0;
};

/// All the ammo types a single weapon (the Nobelisk Detonator, the Rebar Gun) can be loaded
/// with, sharing one "General Capacity" config field alongside their individual overrides.
USTRUCT()
struct FNobeliskPlusCapacityGroup
{
	GENERATED_BODY()

	/// Key of this group's subsection under the configuration's root section, e.g.
	/// "Detonator" or "RebarGun". The group's own fields live under "<ConfigSectionName>/Capacity".
	FString ConfigSectionName;

	UPROPERTY()
	TObjectPtr<UConfigPropertyInteger> GeneralCapacityProperty;

	UPROPERTY()
	TArray<FNobeliskPlusAmmoCapacityTarget> Targets;
};

/// One weapon whose native auto-reload (AFGWeapon::mAutomaticallyReload) is toggled by a
/// config field, so it reloads itself when its magazine empties instead of requiring input.
USTRUCT()
struct FNobeliskPlusAutoReloadTarget
{
	GENERATED_BODY()

	/// Path to the weapon equipment Blueprint, e.g. "/Game/.../Equip_NobeliskDetonator_C".
	FString EquipmentPath;

	/// Key of this target's subsection under the configuration's root section, e.g. "Detonator".
	FString ConfigSectionName;

	UPROPERTY()
	TObjectPtr<AFGWeapon> WeaponCdo;

	UPROPERTY()
	TObjectPtr<UConfigPropertyBool> AutoReloadProperty;
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

	UFUNCTION()
	void OnAutoReloadConfigChanged();

	UFUNCTION()
	void OnCapacityConfigChanged();

	void SetUpShockwaveTarget(FNobeliskPlusShockwaveTarget& target, UConfigPropertySection* configRoot);
	void ApplyShockwaveTarget(const FNobeliskPlusShockwaveTarget& target) const;
	void RegisterPulseRebarAsRebarGunAmmo() const;
	void ClearPulseRebarAmmoDamage() const;
	void SetUpAutoReloadTarget(FNobeliskPlusAutoReloadTarget& target, UConfigPropertySection* configRoot);
	void ApplyAutoReloadTarget(const FNobeliskPlusAutoReloadTarget& target) const;
	void SetUpCapacityGroup(FNobeliskPlusCapacityGroup& group, UConfigPropertySection* configRoot, TArrayView<const FNobeliskDescriptorInfo> descriptors);
	void ApplyCapacityTarget(const FNobeliskPlusAmmoCapacityTarget& target, int32 generalCapacity) const;

	UPROPERTY()
	FNobeliskPlusShockwaveTarget NobeliskTarget;

	UPROPERTY()
	FNobeliskPlusShockwaveTarget RebarTarget;

	/// Patches each weapon's Equip_*'s CDO to enable/disable AFGWeapon's own native
	/// auto-reload (mAutomaticallyReload) per its "Automatically Reload" config toggle.
	/// One entry each for the Nobelisk Detonator and the Rebar Gun.
	UPROPERTY()
	TArray<FNobeliskPlusAutoReloadTarget> AutoReloadTargets;

	/// One entry each for the Nobelisk Detonator and the Rebar Gun's loadable ammo types.
	UPROPERTY()
	TArray<FNobeliskPlusCapacityGroup> CapacityGroups;

	/// Keeps the patched CDO subobjects referenced so they aren't garbage collected.
	UPROPERTY()
	TArray<TObjectPtr<UObject>> CDOEdits;
};
