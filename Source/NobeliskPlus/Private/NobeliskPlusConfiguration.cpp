#include "NobeliskPlusConfiguration.h"

#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
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
	section->SectionProperties.Add(name.ToString(), property);
	return property;
}

/// Adds a "Radius/Impulse/Force Multiplier" trio to the given section, prefixed by the
/// given weapon name (e.g. "Pulse Nobelisk"), and returns the trio's own subsection.
UConfigPropertySection* AddShockwaveSection(UConfigPropertySection* parent, FName name, const TCHAR* weaponDisplayName)
{
	UCP_Section* section = NewObject<UCP_Section>(parent, GetConfigPropertySectionClass(), name);
	section->DisplayName = FText::FromString(weaponDisplayName);
	section->HasHeader = true;
	section->HeaderText = FText::FromString(weaponDisplayName);
	section->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	parent->SectionProperties.Add(name.ToString(), section);

	AddMultiplierProperty(section, TEXT("RadiusMultiplier"), TEXT("Shockwave Radius Multiplier"),
		*FString::Printf(TEXT("How much bigger the %s's shockwave radius should be."), weaponDisplayName), 2.0f);
	AddMultiplierProperty(section, TEXT("ImpulseMultiplier"), TEXT("Shockwave Impulse Multiplier"),
		*FString::Printf(TEXT("How much stronger the %s's one-shot push should be."), weaponDisplayName), 2.0f);
	AddMultiplierProperty(section, TEXT("ForceMultiplier"), TEXT("Shockwave Force Multiplier"),
		*FString::Printf(TEXT("How much stronger the %s's continuous push should be, for any part of the shockwave that isn't delivered as a single impulse."), weaponDisplayName), 2.0f);

	return section;
}
} // namespace

UNobeliskPlusConfiguration::UNobeliskPlusConfiguration(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigId.ModReference = TEXT("NobeliskPlus");
	DisplayName = FText::FromString(TEXT("Nobelisk Plus"));
	Description = FText::FromString(
		TEXT("Tune how much bigger and stronger the Pulse Nobelisk's and Pulse Rebar's shockwaves are. Damage is not affected."));

	UCP_Section* root = NewObject<UCP_Section>(this, GetConfigPropertySectionClass(), TEXT("RootSection"));
	root->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	RootSection = root;

	AddShockwaveSection(root, TEXT("Nobelisk"), TEXT("Pulse Nobelisk"));
	AddShockwaveSection(root, TEXT("Rebar"), TEXT("Pulse Rebar"));
}
