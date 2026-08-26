#include "NobeliskPlusConfiguration.h"

#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Bool.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "NobeliskPlus.h"

namespace
{
// UConfigPropertyFloat/UConfigPropertySection's own CreateEditorWidget() (called by
// UConfigManager::CreateConfigurationWidget to build the mod settings menu) just returns
// null - the actual widget-spawning logic only exists as a Blueprint-graph override on
// SML's BP_ConfigPropertyFloat/BP_ConfigPropertySection assets. Instantiating the bare
// native classes registers and saves correctly (the config "exists"), but produces zero
// visible fields, since every property's widget creation silently no-ops. NewObject must
// be given these Blueprint classes explicitly.
UClass* LoadConfigPropertyBlueprintClass(const TCHAR* path, UClass* nativeFallback)
{
	UClass* loaded = StaticLoadClass(UConfigProperty::StaticClass(), nullptr, path);
	if (loaded == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load %s; falling back to the native class, which won't render any widget in the mod settings menu."), path);
		return nativeFallback;
	}
	return loaded;
}

UClass* GetConfigPropertyFloatClass()
{
	static UClass* cachedClass = LoadConfigPropertyBlueprintClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat.BP_ConfigPropertyFloat_C"),
		UConfigPropertyFloat::StaticClass());
	return cachedClass;
}

UClass* GetConfigPropertySectionClass()
{
	static UClass* cachedClass = LoadConfigPropertyBlueprintClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C"),
		UConfigPropertySection::StaticClass());
	return cachedClass;
}

UClass* GetConfigPropertyBoolClass()
{
	static UClass* cachedClass = LoadConfigPropertyBlueprintClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C"),
		UConfigPropertyBool::StaticClass());
	return cachedClass;
}

UClass* GetConfigPropertyIntegerClass()
{
	static UClass* cachedClass = LoadConfigPropertyBlueprintClass(
		TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyInteger.BP_ConfigPropertyInteger_C"),
		UConfigPropertyInteger::StaticClass());
	return cachedClass;
}

// UCP_Float/UCP_Section - the native (non-Blueprint) classes BP_ConfigPropertyFloat/
// BP_ConfigPropertySection actually derive from - carry the real WidgetType/MinValue/
// MaxValue properties. Left unset, WidgetType/MinValue/MaxValue all default to 0, i.e. a
// Horizontal section layout (every field crammed onto one scrollable line) and a float
// range of [0, 0] (any nonzero value, including our own 2.0 default, reads as out-of-range
// and gets highlighted red / clamped back on edit).
UConfigPropertyFloat* AddMultiplierProperty(
	UConfigPropertySection* section, FName name, const TCHAR* displayName, const TCHAR* tooltip, float defaultValue)
{
	UCP_Float* property = NewObject<UCP_Float>(section, GetConfigPropertyFloatClass(), name);
	property->DisplayName = FText::FromString(displayName);
	property->Tooltip = FText::FromString(tooltip);
	property->DefaultValue = defaultValue;
	property->Value = defaultValue;
	property->WidgetType = ECP_FloatWidgetType::CPF_Spinbox;
	property->MinValue = 0.0f;
	property->MaxValue = 10.0f;
	// Defaults to true on the Blueprint property classes, which locks editing to the main
	// menu only - the whole point of this config is live in-session tuning (values are
	// reapplied immediately via OnPropertyValueChanged), so it must be explicitly cleared.
	property->bRequiresWorldReload = false;
	section->SectionProperties.Add(name.ToString(), property);
	return property;
}

/// Adds a "Radius/Impulse/Force Multiplier" trio to the given section, prefixed by the
/// given weapon name (e.g. "Pulse Nobelisk"), and returns the trio's own subsection.
UConfigPropertySection* AddShockwaveSection(UConfigPropertySection* parent, FName name, const TCHAR* weaponDisplayName, bool includeProjectileSpeed)
{
	UCP_Section* section = NewObject<UCP_Section>(parent, GetConfigPropertySectionClass(), name);
	section->DisplayName = FText::FromString(weaponDisplayName);
	section->HasHeader = true;
	section->HeaderText = FText::FromString(weaponDisplayName);
	section->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	parent->SectionProperties.Add(name.ToString(), section);

	AddMultiplierProperty(section, TEXT("RadiusMultiplier"), TEXT("Shockwave Radius Multiplier"),
		*FString::Printf(TEXT("How much bigger the %s's shockwave radius should be."), weaponDisplayName), 1.0f);
	AddMultiplierProperty(section, TEXT("ImpulseMultiplier"), TEXT("Shockwave Impulse Multiplier"),
		*FString::Printf(TEXT("How much stronger the %s's one-shot push should be."), weaponDisplayName), 1.0f);
	AddMultiplierProperty(section, TEXT("ForceMultiplier"), TEXT("Shockwave Force Multiplier"),
		*FString::Printf(TEXT("How much stronger the %s's continuous push should be, for any part of the shockwave that isn't delivered as a single impulse."), weaponDisplayName), 1.0f);

	if (includeProjectileSpeed)
	{
		AddMultiplierProperty(section, TEXT("SpeedMultiplier"), TEXT("Projectile Speed Multiplier"),
			*FString::Printf(TEXT("How much faster the %s travels after being fired. Raise this if the round feels slow to reach what you aimed at."), weaponDisplayName), 1.0f);
	}

	return section;
}

UConfigPropertyBool* AddBoolProperty(
	UConfigPropertySection* section, FName name, const TCHAR* displayName, const TCHAR* tooltip, bool defaultValue)
{
	UCP_Bool* property = NewObject<UCP_Bool>(section, GetConfigPropertyBoolClass(), name);
	property->DisplayName = FText::FromString(displayName);
	property->Tooltip = FText::FromString(tooltip);
	property->DefaultValue = defaultValue;
	property->Value = defaultValue;
	// See AddMultiplierProperty: defaults to true on the Blueprint property classes, which
	// would lock this to the main menu only, defeating the point of a live-tunable toggle.
	property->bRequiresWorldReload = false;
	section->SectionProperties.Add(name.ToString(), property);
	return property;
}

/// A "0 = no override" capacity field: 0 leaves the magazine size alone (vanilla, or
/// whatever the general capacity below resolves to); any other value replaces it outright.
UConfigPropertyInteger* AddCapacityProperty(
	UConfigPropertySection* section, FName name, const TCHAR* displayName, const TCHAR* tooltip)
{
	UCP_Integer* property = NewObject<UCP_Integer>(section, GetConfigPropertyIntegerClass(), name);
	property->DisplayName = FText::FromString(displayName);
	property->Tooltip = FText::FromString(tooltip);
	property->DefaultValue = 0;
	property->Value = 0;
	property->WidgetType = ECP_IntegerWidgetType::CPI_Spinbox;
	property->MinValue = 0;
	property->MaxValue = 100;
	property->bRequiresWorldReload = false;
	section->SectionProperties.Add(name.ToString(), property);
	return property;
}

/// One capacity override field: its config key, its menu label, and the ammo type name to
/// substitute into the shared tooltip wording.
struct FCapacityFieldInfo
{
	const TCHAR* Key;
	const TCHAR* DisplayName;
	const TCHAR* AmmoNoun;
};

/// Adds an "Automatically Reload" toggle plus a "Magazine Capacity" subsection (one General
/// Capacity field and one override per entry in capacityFields) to a weapon's own section
/// under root. Used for both the Nobelisk Detonator and the Rebar Gun, whose auto-reload and
/// per-ammo-type capacity options work identically - see FNobeliskPlusAutoReloadTarget and
/// FNobeliskPlusCapacityGroup in NobeliskPlusGameInstanceModule.h.
void AddWeaponControlSection(UConfigPropertySection* root, FName sectionKey, const TCHAR* weaponDisplayName,
	const TCHAR* autoReloadTooltip, TArrayView<const FCapacityFieldInfo> capacityFields)
{
	UCP_Section* weaponSection = NewObject<UCP_Section>(root, GetConfigPropertySectionClass(), sectionKey);
	weaponSection->DisplayName = FText::FromString(weaponDisplayName);
	weaponSection->HasHeader = true;
	weaponSection->HeaderText = FText::FromString(weaponDisplayName);
	weaponSection->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	root->SectionProperties.Add(sectionKey.ToString(), weaponSection);

	AddBoolProperty(weaponSection, TEXT("AutoReload"), TEXT("Automatically Reload"), autoReloadTooltip, false);

	UCP_Section* capacitySection = NewObject<UCP_Section>(weaponSection, GetConfigPropertySectionClass(), TEXT("Capacity"));
	capacitySection->DisplayName = FText::FromString(TEXT("Magazine Capacity"));
	capacitySection->HasHeader = true;
	capacitySection->HeaderText = FText::FromString(TEXT("Magazine Capacity"));
	capacitySection->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	weaponSection->SectionProperties.Add(TEXT("Capacity"), capacitySection);

	AddCapacityProperty(capacitySection, TEXT("GeneralCapacity"), TEXT("General Capacity"),
		*FString::Printf(TEXT("If greater than 0, sets how many rounds the %s can hold for every type that doesn't have its own override below. 0 keeps each type's vanilla capacity."), weaponDisplayName));

	for (const FCapacityFieldInfo& field : capacityFields)
	{
		AddCapacityProperty(capacitySection, field.Key, field.DisplayName,
			*FString::Printf(TEXT("Overrides the %s's magazine capacity. 0 = use the General Capacity above (or vanilla if that's also 0)."), field.AmmoNoun));
	}
}
} // namespace

UNobeliskPlusConfiguration::UNobeliskPlusConfiguration(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigId.ModReference = TEXT("NobeliskPlus");
	DisplayName = FText::FromString(TEXT("Nobelisk Plus"));
	Description = FText::FromString(
		TEXT("Tune how much bigger and stronger the Pulse Nobelisk's and Pulse Rebar's shockwaves are, and optionally make the Nobelisk Detonator and Rebar Gun reload themselves and hold more ammo. Every setting defaults to vanilla behaviour. Damage is not affected."));

	UCP_Section* root = NewObject<UCP_Section>(this, GetConfigPropertySectionClass(), TEXT("RootSection"));
	root->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	RootSection = root;

	AddShockwaveSection(root, TEXT("Nobelisk"), TEXT("Pulse Nobelisk"), /* includeProjectileSpeed */ false);
	AddShockwaveSection(root, TEXT("Rebar"), TEXT("Pulse Rebar"), /* includeProjectileSpeed */ true);

	const FCapacityFieldInfo detonatorCapacityFields[] = {
		{ TEXT("ExplosiveCapacity"), TEXT("Standard Nobelisk Capacity"), TEXT("standard Nobelisk") },
		{ TEXT("GasCapacity"), TEXT("Gas Nobelisk Capacity"), TEXT("Gas Nobelisk") },
		{ TEXT("ShockwaveCapacity"), TEXT("Pulse Nobelisk Capacity"), TEXT("Pulse Nobelisk") },
		{ TEXT("ClusterCapacity"), TEXT("Cluster Nobelisk Capacity"), TEXT("Cluster Nobelisk") },
		{ TEXT("NukeCapacity"), TEXT("Nuke Nobelisk Capacity"), TEXT("Nuke Nobelisk") },
	};
	AddWeaponControlSection(root, TEXT("Detonator"), TEXT("Nobelisk Detonator"),
		TEXT("If enabled, the Nobelisk Detonator reloads itself shortly after you throw a charge, instead of requiring a manual reload."),
		detonatorCapacityFields);

	const FCapacityFieldInfo rebarGunCapacityFields[] = {
		{ TEXT("StandardCapacity"), TEXT("Standard Rebar Capacity"), TEXT("standard Rebar") },
		{ TEXT("ExplosiveCapacity"), TEXT("Explosive Rebar Capacity"), TEXT("Explosive Rebar") },
		{ TEXT("SpreadshotCapacity"), TEXT("Spreadshot Rebar Capacity"), TEXT("Spreadshot Rebar") },
		{ TEXT("StunshotCapacity"), TEXT("Stunshot Rebar Capacity"), TEXT("Stunshot Rebar") },
		{ TEXT("PulseCapacity"), TEXT("Pulse Rebar Capacity"), TEXT("Pulse Rebar") },
	};
	AddWeaponControlSection(root, TEXT("RebarGun"), TEXT("Rebar Gun"),
		TEXT("If enabled, the Rebar Gun reloads itself when its magazine empties and spare rounds are available, instead of requiring a manual reload."),
		rebarGunCapacityFields);
}
