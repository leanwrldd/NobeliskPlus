#include "NobeliskPlus.h"

#include "Equipment/FGChargedWeapon.h"
#include "Equipment/FGWeapon.h"
#include "Patching/NativeHookManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogNobeliskPlus)

// The Nobelisk Detonator is an AFGChargedWeapon (charge-and-throw), NOT a plain AFGWeapon
// like the Rebar Gun. Two things that work on ordinary guns therefore do nothing on it, and
// every conclusion below came from runtime logs rather than from reading source -
// FactoryGame's .cpp files are link stubs with empty bodies, so none of this is readable:
//
//  1. Raising the magazine capacity (UFGAmmoType::mMagazineSize) updates the ammo counter,
//     but a reload still loads exactly one round - observed as a stuck "1/5" with plenty of
//     spare Nobelisks in inventory. Fixed by FixChargedWeaponReloadsOnlyOneRound below.
//  2. Enabling AFGWeapon::mAutomaticallyReload has no effect on EITHER weapon.
//
// Case 2 took three attempts, each of which failed for a different reason. Recorded here
// because every one of them looked correct in the source and only the log disproved it:
//
//  a. AFGChargedWeapon::OnAmmoFired is never called. The hook installed successfully
//     ("Successfully hooked function AFGChargedWeapon::OnAmmoFired") yet its body never ran
//     once across a whole session, so a charged weapon never reaches the ammo-fired path at
//     all - it spawns its thrown projectile directly.
//  b. mAutomaticallyReload is EditDefaultsOnly, so an equipment INSTANCE copies it from the
//     class default object when it spawns. Patching the CDO from the config menu therefore
//     never reaches the weapon the player is already holding. Handled by reading the CDO in
//     ShouldAutoReload, plus UNobeliskPlusGameInstanceModule refreshing live instances.
//  c. AFGWeapon::ConsumeAmmunition is never called either, and AFGWeapon::SetWeaponState
//     fired just twice in a session - both times reporting a FULL magazine (ammo=1/1). So
//     the moment the magazine actually empties is never announced by any hookable signal.
//
// (c) is why this no longer tries to catch a "just fired" edge at all. Instead any signal
// that does fire starts a cheap repeating poll on that weapon, and the poll notices the
// magazine has room whenever that becomes true. Polling is unglamorous, but it does not
// depend on guessing which internal path a given weapon subclass takes, which is precisely
// what defeated the three previous attempts.
//
// Reloading itself always re-enters the game's own Reload() rather than writing
// mCurrentAmmoCount directly, so the matching inventory deduction - logic this project has no
// source for - still happens for every round loaded.

namespace
{
/// How often an equipped weapon is checked for "has room and ammo available". Comfortably
/// below human reaction time, and the check is a handful of inline getters.
constexpr float AutoReloadPollInterval = 0.25f;

// Charged weapons only. Plain AFGWeapons already reload to full natively, and hooking them
// there would double up on working behaviour.
bool IsChargedWeapon(const AFGWeapon* weapon)
{
	return weapon != nullptr && weapon->IsA<AFGChargedWeapon>();
}

// A raw TMap<A, B> inside a SUBSCRIBE_*_METHOD_* invocation breaks: those are
// function-like macros, and the preprocessor splits on every top-level comma before it
// ever knows about C++ template syntax, so the comma between A and B reads as an extra
// macro argument. Aliasing the type outside the macro sidesteps that.
using FAmmoAtLastReloadMap = TMap<TWeakObjectPtr<const AFGWeapon>, int32>;
using FAutoReloadPollerMap = TMap<TWeakObjectPtr<const AFGWeapon>, FTimerHandle>;
using FLastLoggedAmmoMap = TMap<TWeakObjectPtr<const AFGWeapon>, int32>;

/// Reads the "Automatically Reload" setting from the weapon's CLASS DEFAULT OBJECT instead of
/// the instance - see (b) above. UNobeliskPlusGameInstanceModule patches the CDO whenever the
/// config changes, so this follows the toggle live even for a weapon that spawned before the
/// change, unlike weapon->GetIsAutomaticallyReloading() which reads the instance's own copy.
bool ShouldAutoReload(const AFGWeapon* weapon)
{
	if (weapon == nullptr || weapon->GetClass() == nullptr)
		return false;

	const AFGWeapon* weaponCdo = weapon->GetClass()->GetDefaultObject<AFGWeapon>();
	return weaponCdo != nullptr && weaponCdo->GetIsAutomaticallyReloading();
}

/// One poll pass for a single weapon. Reloads once the magazine is empty and the game itself
/// says a reload is possible, matching what the vanilla mAutomaticallyReload flag documents.
///
/// This deliberately waits for empty rather than topping the magazine up after every shot.
/// An earlier version reloaded whenever the magazine merely had room, which was wrong twice
/// over: it reloaded on 2/2 -> 1/2, and because Reload() starts a TIMED reload, the next poll
/// 0.25s later restarted it before it could finish - so the magazine never actually refilled
/// and the weapon sat in a permanent reload loop, four calls a second. Firing only at empty
/// leaves the reload alone once it has started, which is what lets it complete.
void PollAutoReload(AFGWeapon* weapon)
{
	if (weapon == nullptr)
		return;

	const int32 currentAmmo = weapon->GetCurrentAmmo();
	const int32 magazineSize = weapon->GetMagSize();

	// Logged only when the count actually changes, so this stays readable rather than emitting
	// four lines a second. This is the diagnostic that was missing while chasing (a) and (c) -
	// it shows what the ammo counter really does across a throw, instead of leaving us to
	// infer it from signals that turned out never to fire.
	static FLastLoggedAmmoMap lastLoggedAmmo;
	static FLastLoggedAmmoMap failedReloadAttempts;
	int32& previousAmmo = lastLoggedAmmo.FindOrAdd(weapon, TNumericLimits<int32>::Min());
	int32& failedAttempts = failedReloadAttempts.FindOrAdd(weapon, 0);

	if (previousAmmo != currentAmmo)
	{
		// A round actually arrived, so reloading demonstrably works for this weapon - clear
		// the give-up counter below. Doing it here rather than at the reload site is what
		// keeps the counter from creeping up across normal fire/reload cycles, where the ammo
		// returns to the same values over and over.
		if (currentAmmo > previousAmmo)
		{
			failedAttempts = 0;
		}

		previousAmmo = currentAmmo;
		UE_LOG(LogNobeliskPlus, Log, TEXT("%s ammo is now %d/%d (autoReload=%d, canReload=%d, reloading=%d)"),
			*weapon->GetName(), currentAmmo, magazineSize,
			ShouldAutoReload(weapon) ? 1 : 0, weapon->CanReload() ? 1 : 0, weapon->GetIsReloading() ? 1 : 0);
	}

	if (!ShouldAutoReload(weapon))
		return;

	// A magazine size of -1 means no ammo class is selected (see AFGWeapon::GetMagSize), which
	// is not a weapon that can be reloaded into anything meaningful. Anything still holding a
	// round is left alone - see the note above on why "empty" and not "has room".
	if (magazineSize <= 0 || currentAmmo > 0)
		return;

	// Reload() while a reload is already running would restart it every poll and the weapon
	// would never finish. CanReload() covers the rest: mid-throw, no spare ammo, and so on.
	if (weapon->GetIsReloading() || !weapon->CanReload())
		return;

	// Backstop against the reload loop described above. GetIsReloading() is the first line of
	// defence, but it was observed reporting false while a reload was in flight, so a weapon
	// whose ammo count never rises would otherwise be told to reload four times a second
	// indefinitely. failedAttempts is reset above the moment a round actually arrives, so this
	// only ever trips when reloading genuinely is not working - turning what was an endless
	// loop into a bounded, logged stop.
	constexpr int32 maxFailedAttempts = 8; // ~2s at the poll interval, longer than a reload

	if (failedAttempts >= maxFailedAttempts)
	{
		if (failedAttempts == maxFailedAttempts)
		{
			++failedAttempts; // logs once, then stays silent until a reload succeeds
			UE_LOG(LogNobeliskPlus, Warning,
				TEXT("%s stayed at %d/%d after %d auto-reload attempts; giving up until its ammo changes. Auto-reload may need revisiting for this game version."),
				*weapon->GetName(), currentAmmo, magazineSize, maxFailedAttempts);
		}
		return;
	}

	++failedAttempts;
	UE_LOG(LogNobeliskPlus, Log, TEXT("Auto-reloading %s (%d/%d)."), *weapon->GetName(), currentAmmo, magazineSize);
	weapon->Reload();
}

/// Starts the repeating poll for a weapon the first time any hook sees it. Called from several
/// unrelated hooks on purpose: it only takes ONE of them to ever fire for that weapon to be
/// polled from then on, which is what makes this robust to signals that turn out to be dead.
void EnsureAutoReloadPollerRunning(AFGWeapon* weapon, const TCHAR* signalName)
{
	if (weapon == nullptr)
		return;

	UWorld* world = weapon->GetWorld();
	if (world == nullptr)
		return;

	static FAutoReloadPollerMap autoReloadPollers;
	FTimerHandle& timerHandle = autoReloadPollers.FindOrAdd(weapon);
	if (world->GetTimerManager().IsTimerActive(timerHandle))
		return;

	UE_LOG(LogNobeliskPlus, Log, TEXT("Watching %s for auto-reload (first seen via '%s')."), *weapon->GetName(), signalName);

	// CreateWeakLambda ties the timer's lifetime to the weapon: once the equipment actor is
	// destroyed the callback stops firing, so a dropped or unequipped weapon cleans itself up.
	world->GetTimerManager().SetTimer(timerHandle,
		FTimerDelegate::CreateWeakLambda(weapon, [weapon]() { PollAutoReload(weapon); }),
		AutoReloadPollInterval, /* loop */ true);
}
} // namespace

void FNobeliskPlusModule::StartupModule()
{
	// Native hooks patch the running game's code and are meaningless in the editor, where
	// there is no game process to patch - matching how other mods in this project gate them.
	if constexpr (!WITH_EDITOR)
	{
		FixChargedWeaponReloadsOnlyOneRound();
		FixWeaponIgnoresAutoReload();
	}
}

void FNobeliskPlusModule::ShutdownModule()
{
}

void FNobeliskPlusModule::FixChargedWeaponReloadsOnlyOneRound()
{
	// ActualReload is where a round actually lands in the magazine. Re-entering Reload()
	// afterwards tops the magazine up one round at a time until CanReload() goes false
	// (magazine full, or no spare ammo left).
	//
	// At vanilla capacity this is inert: one round fills the magazine, CanReload() returns
	// false, and the hook stops immediately without a second reload.
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGWeapon, ActualReload,
		[](AFGWeapon* weapon)
		{
			// Confirmed to fire reliably for the Nobelisk Detonator, which makes it the most
			// dependable place to start watching a weapon.
			EnsureAutoReloadPollerRunning(weapon, TEXT("ActualReload"));

			if (!IsChargedWeapon(weapon))
				return;

			const int32 currentAmmo = weapon->GetCurrentAmmo();

			// Safety guard, not an optimisation. Re-entering Reload() assumes each pass adds
			// a round; if a future game version made ActualReload a no-op for charged weapons
			// that assumption becomes an endless reload loop that hangs the game. Requiring
			// the count to have actually risen since this weapon's last pass makes the
			// failure mode "stops early" instead of "freezes".
			static FAmmoAtLastReloadMap ammoAtLastReload;
			const int32* previousAmmo = ammoAtLastReload.Find(weapon);
			const bool bMadeProgress = previousAmmo == nullptr || currentAmmo > *previousAmmo;

			if (!bMadeProgress)
			{
				UE_LOG(LogNobeliskPlus, Warning,
					TEXT("%s reloaded but stayed at %d/%d; stopping rather than looping. The charged-weapon reload fix may need revisiting for this game version."),
					*weapon->GetName(), currentAmmo, weapon->GetMagSize());
				ammoAtLastReload.Remove(weapon);
				return;
			}

			if (!weapon->CanReload())
			{
				UE_LOG(LogNobeliskPlus, Log, TEXT("%s finished reloading at %d/%d; nothing more to load."),
					*weapon->GetName(), currentAmmo, weapon->GetMagSize());
				ammoAtLastReload.Remove(weapon);
				return;
			}

			ammoAtLastReload.Add(weapon, currentAmmo);
			UE_LOG(LogNobeliskPlus, Log, TEXT("%s is at %d/%d after reloading; loading another round."),
				*weapon->GetName(), currentAmmo, weapon->GetMagSize());
			weapon->Reload();
		});
}

void FNobeliskPlusModule::FixWeaponIgnoresAutoReload()
{
	// These hooks no longer trigger the reload themselves - they only need to notice a weapon
	// exists so the poll can start. That is a much weaker requirement than firing at the right
	// instant, which is why it survives signals being dead or sparse: ConsumeAmmunition never
	// fired at all in testing, and SetWeaponState fired twice in a session. Either is plenty
	// to start a poller, whereas neither was usable as the reload trigger itself.

	SUBSCRIBE_METHOD_AFTER(AFGWeapon::ConsumeAmmunition,
		[](AFGWeapon* weapon)
		{
			EnsureAutoReloadPollerRunning(weapon, TEXT("ConsumeAmmunition"));
		});

	// Also covers equipping, which transitions the weapon into Standby - so a weapon that has
	// not fired yet still starts polling. AFGWeapon::Equip would be the more direct signal but
	// CANNOT be hooked here: it is a virtual override, and SML needs a sample object instance
	// to resolve an override's address, which does not exist during StartupModule. Hooking it
	// anyway compiles cleanly and then fails at launch with "Attempt to hook virtual function
	// override without providing object instance for implementation resolution". Every hook in
	// this file is therefore a non-virtual method, which needs no instance.
	SUBSCRIBE_METHOD_AFTER(AFGWeapon::SetWeaponState,
		[](AFGWeapon* weapon, EWeaponState newState)
		{
			EnsureAutoReloadPollerRunning(weapon, TEXT("SetWeaponState"));
		});
}

IMPLEMENT_MODULE(FNobeliskPlusModule, NobeliskPlus)
