#include "NobeliskPlusConfiguration.h"

#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertySection.h"

namespace
{
UConfigPropertyFloat* AddMultiplierProperty(
	UConfigPropertySection* section, FName name, const TCHAR* displayName, const TCHAR* tooltip, float defaultValue)
{
	UConfigPropertyFloat* property = NewObject<UConfigPropertyFloat>(section, name);
	property->DisplayName = FText::FromString(displayName);
	property->Tooltip = FText::FromString(tooltip);
	property->DefaultValue = defaultValue;
	property->Value = defaultValue;
	section->SectionProperties.Add(name.ToString(), property);
	return property;
}

/// Adds a "Radius/Impulse/Force Multiplier" trio to the given section, prefixed by the
/// given weapon name (e.g. "Pulse Nobelisk"), and returns the trio's own subsection.
UConfigPropertySection* AddShockwaveSection(UConfigPropertySection* parent, FName name, const TCHAR* weaponDisplayName)
{
	UConfigPropertySection* section = NewObject<UConfigPropertySection>(parent, name);
	section->DisplayName = FText::FromString(weaponDisplayName);
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

	UConfigPropertySection* root = NewObject<UConfigPropertySection>(this, TEXT("RootSection"));
	RootSection = root;

	AddShockwaveSection(root, TEXT("Nobelisk"), TEXT("Pulse Nobelisk"));
	AddShockwaveSection(root, TEXT("Rebar"), TEXT("Pulse Rebar"));
}
