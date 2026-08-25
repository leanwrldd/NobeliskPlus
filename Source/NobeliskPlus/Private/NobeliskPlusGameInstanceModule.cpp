#include "NobeliskPlusGameInstanceModule.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/EngineTypes.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Equipment/FGWeapon.h"
#include "NobeliskPlus.h"
#include "NobeliskPlusConfiguration.h"
#include "NobeliskPlusPulseRebarProjectile.h"
#include "PhysicsEngine/RadialForceComponent.h"
#include "Reflection/ReflectionHelper.h"
#include "UObject/UnrealType.h"

namespace
{
// The shockwave on both projectiles is driven entirely by a native
// URadialForceComponent (Blueprint variable "RadialForce"); damage is handled
// separately (AFGProjectile::mDamageTypesAtEndOfLife), so patching this component
// cannot affect damage.
constexpr const TCHAR* PulseNobeliskProjectilePath =
	TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/BP_NobeliskShockwave.BP_NobeliskShockwave_C");
constexpr const TCHAR* PulseRebarProjectilePath =
	TEXT("/NobeliskPlus/Ammo/BP_Rebar_Pulse.BP_Rebar_Pulse_C");
constexpr const TCHAR* PulseRebarAmmoDescriptorPath =
	TEXT("/NobeliskPlus/Ammo/Desc_Rebar_Pulse.Desc_Rebar_Pulse_C");
// The Rebar Gun doesn't accept every UFGAmmoType-derived item automatically - it whitelists
// them explicitly on the equipment Blueprint (mAllowedAmmoClasses), which is why a new ammo
// type otherwise never shows up as loadable, even with a valid recipe and MAM unlock.
constexpr const TCHAR* RebarGunEquipmentPath =
	TEXT("/Game/FactoryGame/Equipment/RebarGun/Equip_RebarGun_Projectile.Equip_RebarGun_Projectile_C");

UConfigPropertyFloat* FindMultiplierProperty(UConfigPropertySection* section, const TCHAR* name)
{
	return section != nullptr ? Cast<UConfigPropertyFloat>(section->SectionProperties.FindRef(name)) : nullptr;
}

// RadialForce is a Blueprint Simple-Construction-Script component (added via the
// Components panel), not a component the native base class constructs in its own
// constructor. SCS components are NOT attached to a Blueprint class's CDO - they only
// get instanced onto actual spawned actors via the construction script - so
// cdo->FindComponentByClass<URadialForceComponent>() always silently returns null here,
// regardless of the component visibly existing in the Blueprint. What every spawned
// instance actually copies its property values from is the SCS node's ComponentTemplate,
// which is what needs patching instead.
URadialForceComponent* FindRadialForceComponentTemplate(UClass* actorClass)
{
	for (UClass* currentClass = actorClass; currentClass != nullptr; currentClass = currentClass->GetSuperClass())
	{
		const UBlueprintGeneratedClass* blueprintClass = Cast<UBlueprintGeneratedClass>(currentClass);
		if (blueprintClass == nullptr || blueprintClass->SimpleConstructionScript == nullptr)
			continue;

		for (USCS_Node* node : blueprintClass->SimpleConstructionScript->GetAllNodes())
		{
			if (URadialForceComponent* component = Cast<URadialForceComponent>(node->ComponentTemplate))
			{
				return component;
			}
		}
	}
	return nullptr;
}

// ObjectTypesToAffect is protected on URadialForceComponent, so - like mAllowedAmmoClasses
// on AFGWeapon - it's reached through reflection rather than direct member access.
// TEnumAsByte<EObjectTypeQuery> surfaces as a plain FByteProperty array element.
void EnsureObjectTypesToAffectIncludesPlayer(URadialForceComponent& radialForce, const FString& projectilePath)
{
	FArrayProperty* objectTypesProp = FReflectionHelper::FindPropertyChecked<FArrayProperty>(URadialForceComponent::StaticClass(), TEXT("ObjectTypesToAffect"));
	FByteProperty* elementProp = CastField<FByteProperty>(objectTypesProp->Inner);
	check(elementProp != nullptr);

	FScriptArrayHelper arrayHelper(objectTypesProp, objectTypesProp->ContainerPtrToValuePtr<void>(&radialForce));

	// Checking Num() == 0 to decide "unconfigured" was wrong: Unreal only serializes a
	// property into the .uasset when it differs from its class default, and
	// URadialForceComponent's own C++ default for this array is apparently already
	// non-empty (it survived unnoticed because nothing here ever logged the values it
	// found) - so BP_Rebar_Explosive silently inheriting that default, never having
	// explicitly added Pawn, looked identical to "already configured" under a Num()>0
	// check. Check for Pawn specifically instead.
	const uint8 pawnByte = static_cast<uint8>(UEngineTypes::ConvertToObjectType(ECC_Pawn));
	for (int32 index = 0; index < arrayHelper.Num(); ++index)
	{
		if (elementProp->GetPropertyValue(arrayHelper.GetRawPtr(index)) == pawnByte)
		{
			UE_LOG(LogNobeliskPlus, Log, TEXT("%s's RadialForceComponent already affects Pawns (%d object type(s) configured)."), *projectilePath, arrayHelper.Num());
			return;
		}
	}

	// The Radius/Impulse/Force values only decide how strong the push is; whether it
	// affects a player at all is gated separately by ObjectTypesToAffect, which
	// BP_Rebar_Explosive's component never had Pawn added to (it's set up for a bit of
	// debris knock on impact, not for pushing the player - unlike BP_NobeliskShockwave's,
	// which already includes Pawn). Match Nobelisk's set so the Pulse Rebar behaves the
	// same way, replacing whatever was there rather than appending to avoid duplicates.
	const EObjectTypeQuery objectTypes[] = {
		UEngineTypes::ConvertToObjectType(ECC_WorldDynamic),
		UEngineTypes::ConvertToObjectType(ECC_Pawn),
		UEngineTypes::ConvertToObjectType(ECC_PhysicsBody),
		UEngineTypes::ConvertToObjectType(ECC_Vehicle),
		UEngineTypes::ConvertToObjectType(ECC_Destructible),
	};
	const int32 previousNum = arrayHelper.Num();
	arrayHelper.Resize(UE_ARRAY_COUNT(objectTypes));
	for (int32 index = 0; index < UE_ARRAY_COUNT(objectTypes); ++index)
	{
		elementProp->SetPropertyValue(arrayHelper.GetRawPtr(index), static_cast<uint8>(objectTypes[index]));
	}

	UE_LOG(LogNobeliskPlus, Log, TEXT("%s's RadialForceComponent did not affect Pawns (had %d object type(s) configured, none of them Pawn); replaced with Pawn/PhysicsBody/Vehicle/Destructible/WorldDynamic."), *projectilePath, previousNum);
}
} // namespace

UNobeliskPlusGameInstanceModule::UNobeliskPlusGameInstanceModule()
{
	// Without this, FPluginModuleLoader::FindRootModulesOfType never discovers this
	// class and DispatchLifecycleEvent is never called on it at all.
	bRootModule = true;

	ModConfigurations.Add(UNobeliskPlusConfiguration::StaticClass());

	NobeliskTarget.ProjectilePath = PulseNobeliskProjectilePath;
	NobeliskTarget.ConfigSectionName = TEXT("Nobelisk");

	RebarTarget.ProjectilePath = PulseRebarProjectilePath;
	RebarTarget.ConfigSectionName = TEXT("Rebar");
	RebarTarget.bEnsurePlayerCanBePushed = true;
	RebarTarget.bDrivesNativePulse = true;
}

void UNobeliskPlusGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	Super::DispatchLifecycleEvent(phase);

	if (phase != ELifecyclePhase::POST_INITIALIZATION)
		return;

	check(CDOEdits.IsEmpty());

	UConfigManager* configManager = GetGameInstance()->GetSubsystem<UConfigManager>();
	UConfigPropertySection* configRoot = nullptr;
	if (configManager != nullptr)
	{
		FConfigId configId;
		configId.ModReference = GetOwnerModReference().ToString();
		configRoot = configManager->GetConfigurationRootSection(configId);
	}
	else
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not find the config manager; shockwave multipliers will use their defaults and won't be tweakable in the mod settings menu."));
	}

	SetUpShockwaveTarget(NobeliskTarget, configRoot);
	SetUpShockwaveTarget(RebarTarget, configRoot);
	RegisterPulseRebarAsRebarGunAmmo();
	ClearPulseRebarAmmoDamage();
}

void UNobeliskPlusGameInstanceModule::RegisterPulseRebarAsRebarGunAmmo() const
{
	UClass* rebarGunClass = StaticLoadClass(AFGWeapon::StaticClass(), nullptr, RebarGunEquipmentPath);
	UClass* pulseRebarDescriptorClass = StaticLoadClass(UObject::StaticClass(), nullptr, PulseRebarAmmoDescriptorPath);
	if (rebarGunClass == nullptr || pulseRebarDescriptorClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the Rebar Gun and/or the Pulse Rebar ammo descriptor; the Pulse Rebar won't be loadable into the Rebar Gun."));
		return;
	}

	// mAllowedAmmoClasses is protected on AFGWeapon, so it's appended to via reflection
	// rather than direct member access.
	AFGWeapon* rebarGunCdo = rebarGunClass->GetDefaultObject<AFGWeapon>();
	FArrayProperty* allowedAmmoProp = FReflectionHelper::FindPropertyChecked<FArrayProperty>(AFGWeapon::StaticClass(), TEXT("mAllowedAmmoClasses"));
	FClassProperty* elementProp = CastField<FClassProperty>(allowedAmmoProp->Inner);
	check(elementProp != nullptr);

	FScriptArrayHelper arrayHelper(allowedAmmoProp, allowedAmmoProp->ContainerPtrToValuePtr<void>(rebarGunCdo));
	for (int32 index = 0; index < arrayHelper.Num(); ++index)
	{
		if (elementProp->GetObjectPropertyValue(arrayHelper.GetRawPtr(index)) == pulseRebarDescriptorClass)
		{
			return; // already present (e.g. a second call within the same process)
		}
	}

	const int32 newIndex = arrayHelper.AddValue();
	elementProp->SetObjectPropertyValue(arrayHelper.GetRawPtr(newIndex), pulseRebarDescriptorClass);

	UE_LOG(LogNobeliskPlus, Log, TEXT("Added the Pulse Rebar to the Rebar Gun's allowed ammo classes."));
}

void UNobeliskPlusGameInstanceModule::SetUpShockwaveTarget(FNobeliskPlusShockwaveTarget& target, UConfigPropertySection* configRoot)
{
	UClass* projectileClass = StaticLoadClass(AActor::StaticClass(), nullptr, *target.ProjectilePath);
	if (projectileClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the projectile class at %s; its shockwave was not amplified."), *target.ProjectilePath);
		return;
	}

	target.RadialForce = FindRadialForceComponentTemplate(projectileClass);
	if (target.RadialForce == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("%s no longer has a RadialForceComponent; its shockwave was not amplified."), *target.ProjectilePath);
		return;
	}

	target.BaseRadius = target.RadialForce->Radius;
	target.BaseImpulseStrength = target.RadialForce->ImpulseStrength;
	target.BaseForceStrength = target.RadialForce->ForceStrength;
	CDOEdits.Add(target.RadialForce);

	if (target.bEnsurePlayerCanBePushed)
	{
		EnsureObjectTypesToAffectIncludesPlayer(*target.RadialForce, target.ProjectilePath);
	}

	UConfigPropertySection* section = configRoot != nullptr
		? Cast<UConfigPropertySection>(configRoot->SectionProperties.FindRef(target.ConfigSectionName))
		: nullptr;
	target.RadiusMultiplierProperty = FindMultiplierProperty(section, TEXT("RadiusMultiplier"));
	target.ImpulseMultiplierProperty = FindMultiplierProperty(section, TEXT("ImpulseMultiplier"));
	target.ForceMultiplierProperty = FindMultiplierProperty(section, TEXT("ForceMultiplier"));

	for (UConfigPropertyFloat* property : {target.RadiusMultiplierProperty, target.ImpulseMultiplierProperty, target.ForceMultiplierProperty})
	{
		if (property != nullptr)
		{
			property->OnPropertyValueChanged.AddDynamic(this, &UNobeliskPlusGameInstanceModule::OnShockwaveConfigChanged);
		}
	}

	ApplyShockwaveTarget(target);
}

void UNobeliskPlusGameInstanceModule::OnShockwaveConfigChanged()
{
	ApplyShockwaveTarget(NobeliskTarget);
	ApplyShockwaveTarget(RebarTarget);
}

void UNobeliskPlusGameInstanceModule::ApplyShockwaveTarget(const FNobeliskPlusShockwaveTarget& target) const
{
	if (target.RadialForce == nullptr)
		return;

	const float radiusMultiplier = target.RadiusMultiplierProperty ? target.RadiusMultiplierProperty->Value : 1.0f;
	const float impulseMultiplier = target.ImpulseMultiplierProperty ? target.ImpulseMultiplierProperty->Value : 1.0f;
	const float forceMultiplier = target.ForceMultiplierProperty ? target.ForceMultiplierProperty->Value : 1.0f;

	UE_LOG(LogNobeliskPlus, Log,
		TEXT("Applying shockwave config for %s: radius %.0f -> %.0f, impulse strength %.0f -> %.0f, force strength %.0f -> %.0f"),
		*target.ProjectilePath,
		target.BaseRadius, target.BaseRadius * radiusMultiplier,
		target.BaseImpulseStrength, target.BaseImpulseStrength * impulseMultiplier,
		target.BaseForceStrength, target.BaseForceStrength * forceMultiplier);

	target.RadialForce->Radius = target.BaseRadius * radiusMultiplier;
	target.RadialForce->ImpulseStrength = target.BaseImpulseStrength * impulseMultiplier;
	target.RadialForce->ForceStrength = target.BaseForceStrength * forceMultiplier;

	if (target.bDrivesNativePulse)
	{
		// The Blueprint's RadialForceComponent is left configured above for physics debris,
		// but the push players actually feel comes from ANobeliskPlusPulseRebarProjectile.
		// Base launch velocity is its own number because LaunchCharacter takes cm/s, not an
		// impulse - the component's impulse figure would be meaningless there.
		constexpr float baseLaunchVelocity = 1200.0f;
		ANobeliskPlusPulseRebarProjectile::PulseRadius = target.BaseRadius * radiusMultiplier;
		ANobeliskPlusPulseRebarProjectile::PulseLaunchVelocity = baseLaunchVelocity * impulseMultiplier;
		ANobeliskPlusPulseRebarProjectile::PulsePhysicsImpulse = target.BaseImpulseStrength * impulseMultiplier;
	}
}

void UNobeliskPlusGameInstanceModule::ClearPulseRebarAmmoDamage() const
{
	// Damage does NOT come from the projectile Blueprint. AFGProjectile's own
	// mDamageTypesOnImpact/mDamageTypesAtEndOfLife are empty on BP_Rebar_Explosive too -
	// clearing them on BP_Rebar_Pulse was always a no-op. The damage types actually live on
	// the AMMO DESCRIPTOR (UFGAmmoType::mDamageTypesOnImpact and
	// UFGAmmoTypeProjectile::mDamageTypesAtEndOfLife) and are pushed onto each spawned
	// projectile at fire time via SetupProjectile/SetImpactDamageTypes. Desc_Rebar_Pulse
	// inherited BP_PointDamageType_Physical + BP_RadialDamageType_Explosive from the
	// duplicated Desc_Rebar_Explosive, which is why the Pulse Rebar still hurt.
	// Both properties are protected, so they are cleared through reflection.
	UClass* ammoClass = StaticLoadClass(UObject::StaticClass(), nullptr, PulseRebarAmmoDescriptorPath);
	if (ammoClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the Pulse Rebar ammo descriptor; it will still deal damage."));
		return;
	}

	UObject* ammoCdo = ammoClass->GetDefaultObject();
	for (const TCHAR* propertyName : {TEXT("mDamageTypesOnImpact"), TEXT("mDamageTypesAtEndOfLife")})
	{
		FArrayProperty* damageTypesProp = CastField<FArrayProperty>(ammoClass->FindPropertyByName(propertyName));
		if (damageTypesProp == nullptr)
		{
			UE_LOG(LogNobeliskPlus, Warning, TEXT("Pulse Rebar ammo descriptor has no %s property; skipping it."), propertyName);
			continue;
		}

		FScriptArrayHelper arrayHelper(damageTypesProp, damageTypesProp->ContainerPtrToValuePtr<void>(ammoCdo));
		const int32 previousNum = arrayHelper.Num();
		if (previousNum == 0)
			continue;

		arrayHelper.EmptyValues();
		UE_LOG(LogNobeliskPlus, Log, TEXT("Cleared %d damage type(s) from the Pulse Rebar ammo descriptor's %s; it is now push-only."), previousNum, propertyName);
	}
}
