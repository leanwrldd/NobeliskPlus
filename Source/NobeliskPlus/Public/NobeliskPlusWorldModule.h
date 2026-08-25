#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "NobeliskPlusWorldModule.generated.h"

/// Registers the Pulse Rebar's MAM research tree (ResearchTree_PulseRebar), so it can be
/// researched and its recipe unlocked the same way the base game's other rebar variants
/// are unlocked through the MAM.
UCLASS()
class NOBELISKPLUS_API UNobeliskPlusWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UNobeliskPlusWorldModule();

	// UGameWorldModule
	virtual void DispatchLifecycleEvent(ELifecyclePhase phase) override;
};
