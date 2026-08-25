#include "NobeliskPlusGameInstanceModule.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "NobeliskPlus.h"
#include "NobeliskPlusConfiguration.h"
#include "PhysicsEngine/RadialForceComponent.h"

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

UConfigPropertyFloat* FindMultiplierProperty(UConfigPropertySection* section, const TCHAR* name)
{
	return section != nullptr ? Cast<UConfigPropertyFloat>(section->SectionProperties.FindRef(name)) : nullptr;
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
}

void UNobeliskPlusGameInstanceModule::SetUpShockwaveTarget(FNobeliskPlusShockwaveTarget& target, UConfigPropertySection* configRoot)
{
	UClass* projectileClass = StaticLoadClass(AActor::StaticClass(), nullptr, *target.ProjectilePath);
	if (projectileClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the projectile class at %s; its shockwave was not amplified."), *target.ProjectilePath);
		return;
	}

	AActor* cdo = projectileClass->GetDefaultObject<AActor>();
	target.RadialForce = cdo->FindComponentByClass<URadialForceComponent>();
	if (target.RadialForce == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("%s no longer has a RadialForceComponent; its shockwave was not amplified."), *target.ProjectilePath);
		return;
	}

	target.BaseRadius = target.RadialForce->Radius;
	target.BaseImpulseStrength = target.RadialForce->ImpulseStrength;
	target.BaseForceStrength = target.RadialForce->ForceStrength;
	CDOEdits.Add(target.RadialForce);

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
}
