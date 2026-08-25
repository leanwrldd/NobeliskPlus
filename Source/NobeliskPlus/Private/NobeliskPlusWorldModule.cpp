#include "NobeliskPlusWorldModule.h"

#include "FGResearchTree.h"
#include "NobeliskPlus.h"

namespace
{
constexpr const TCHAR* PulseRebarResearchTreePath = TEXT("/NobeliskPlus/ResearchTree_PulseRebar.ResearchTree_PulseRebar_C");
}

UNobeliskPlusWorldModule::UNobeliskPlusWorldModule()
{
	// Without this, FPluginModuleLoader::FindRootModulesOfType never discovers this
	// class and DispatchLifecycleEvent is never called on it at all.
	bRootModule = true;
}

void UNobeliskPlusWorldModule::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	// mResearchTrees is consumed by the base class during the (separate, later) INITIALIZATION
	// phase call, so it must be populated before then; CONSTRUCTION is the earliest opportunity.
	if (phase == ELifecyclePhase::CONSTRUCTION)
	{
		UClass* researchTreeClass = StaticLoadClass(UFGResearchTree::StaticClass(), nullptr, PulseRebarResearchTreePath);
		if (researchTreeClass != nullptr)
		{
			mResearchTrees.Add(researchTreeClass);
		}
		else
		{
			UE_LOG(LogNobeliskPlus, Error,
				TEXT("Could not load the Pulse Rebar research tree at %s; it won't be available in the MAM."),
				PulseRebarResearchTreePath);
		}
	}

	Super::DispatchLifecycleEvent(phase);
}
