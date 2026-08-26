#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNobeliskPlus, Log, All)

class FNobeliskPlusModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/// Makes a charged weapon (the Nobelisk Detonator) keep reloading until its magazine is
	/// full, rather than stopping after a single round.
	static void FixChargedWeaponReloadsOnlyOneRound();

	/// Makes weapons honour the "Automatically Reload" config toggle. Neither weapon manages
	/// it natively: a charged weapon never reaches the ammo-fired path at all, and an
	/// already-spawned instance keeps the mAutomaticallyReload value it copied from its class
	/// default object when it spawned.
	static void FixWeaponIgnoresAutoReload();
};
