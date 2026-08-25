#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "NobeliskPlusWorldModule.generated.h"

/// Injects a Pulse Rebar research node directly into the base game's Quartz MAM tree,
/// right after the Pulse Nobelisk node, instead of registering a separate tree. The
/// Quartz tree is base game content we don't own, so this patches its class default
/// object at runtime (via UFGResearchTree::GetNodes/SetNodes) rather than editing the
/// vanilla asset, which wouldn't ship inside this plugin's pak anyway.
UCLASS()
class NOBELISKPLUS_API UNobeliskPlusWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UNobeliskPlusWorldModule();

	// UGameWorldModule
	virtual void DispatchLifecycleEvent(ELifecyclePhase phase) override;

private:
	void AddPulseRebarNodeToQuartzTree();
};
