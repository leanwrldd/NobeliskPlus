#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "NobeliskPlusConfiguration.generated.h"

/// Defines the in-game mod settings for Nobelisk Plus: how much bigger/stronger the
/// Pulse Nobelisk's shockwave should be. Values are read live, so changes made from
/// the mod settings menu take effect immediately without restarting.
UCLASS()
class NOBELISKPLUS_API UNobeliskPlusConfiguration : public UModConfiguration
{
	GENERATED_BODY()

public:
	UNobeliskPlusConfiguration(const FObjectInitializer& ObjectInitializer);
};
