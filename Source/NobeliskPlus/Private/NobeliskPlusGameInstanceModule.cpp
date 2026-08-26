#include "NobeliskPlusGameInstanceModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/StaticMesh.h"
#include "Engine/EngineTypes.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "DamageTypes/FGDamageType.h"
#include "Materials/MaterialInterface.h"
#include "Equipment/FGAmmoType.h"
#include "Equipment/FGWeapon.h"
#include "FGProjectileMovementComponent.h"
#include "NobeliskPlus.h"
#include "NobeliskPlusConfiguration.h"
#include "NiagaraSystem.h"
#include "NobeliskPlusPulseRebarProjectile.h"
#include "Particles/ParticleSystem.h"
#include "PhysicsEngine/RadialForceComponent.h"
#include "Reflection/ReflectionHelper.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

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
constexpr const TCHAR* PulseRebarAmmoDescriptorPath =
	TEXT("/NobeliskPlus/Ammo/Desc_Rebar_Pulse.Desc_Rebar_Pulse_C");
// The Rebar Gun doesn't accept every UFGAmmoType-derived item automatically - it whitelists
// them explicitly on the equipment Blueprint (mAllowedAmmoClasses), which is why a new ammo
// type otherwise never shows up as loadable, even with a valid recipe and MAM unlock.
// BP_Rebar_Pulse was duplicated from BP_Rebar_Explosive, whose real parent is the shared
// Blueprint BP_RebarProjectile (NOT AFGProjectile directly) - that base is where the rebar
// mesh and trail live. The duplicate ended up parented straight at the native class, so it
// carries its own copy of the "Rebar" StaticMeshComponent but with no mesh assigned, which is
// why the round is invisible in flight. Assign the same mesh the shared base uses.
// The magazine (the rounds visible loaded in the gun) is drawn with the ammo descriptor's
// mMagazineMeshMaterials / mMagazineMeshMaterials1p. Duplicating Desc_Rebar_Explosive carried
// its red-tipped explosive materials over. The Stun rebar's set is the blue/electric one,
// which suits a "pulse" round and matches a cyan icon; swap to it.
constexpr const TCHAR* PulseRebarMagazineMaterialPath =
	TEXT("/Game/FactoryGame/Equipment/RebarGun/Material/MI_RebarStun_01.MI_RebarStun_01");
constexpr const TCHAR* PulseRebarMagazineMaterial1pPath =
	TEXT("/Game/FactoryGame/Equipment/RebarGun/Material/MI_RebarStun_01_1p.MI_RebarStun_01_1p");
constexpr const TCHAR* RebarProjectileMeshPath =
	TEXT("/Game/FactoryGame/Equipment/RebarGun/Mesh/Rebar_static_01.Rebar_static_01");
constexpr const TCHAR* RebarGunEquipmentPath =
	TEXT("/Game/FactoryGame/Equipment/RebarGun/Equip_RebarGun_Projectile.Equip_RebarGun_Projectile_C");
constexpr const TCHAR* NobeliskDetonatorEquipmentPath =
	TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Equip_NobeliskDetonator.Equip_NobeliskDetonator_C");

// The ammo descriptors each weapon's mAllowedAmmoClasses actually lists (confirmed by
// grepping each equipment Blueprint's own class references, since mAllowedAmmoClasses is
// populated by its construction script rather than a reflectable CDO array default).
const FNobeliskDescriptorInfo NobeliskDetonatorDescriptors[] = {
	{ TEXT("/Game/FactoryGame/Resource/Parts/NobeliskExplosive/Desc_NobeliskExplosive.Desc_NobeliskExplosive_C"), TEXT("ExplosiveCapacity") },
	{ TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskGas.Desc_NobeliskGas_C"), TEXT("GasCapacity") },
	{ TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskShockwave.Desc_NobeliskShockwave_C"), TEXT("ShockwaveCapacity") },
	{ TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskCluster.Desc_NobeliskCluster_C"), TEXT("ClusterCapacity") },
	{ TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskNuke.Desc_NobeliskNuke_C"), TEXT("NukeCapacity") },
};

// Includes Desc_Rebar_Pulse - the ammo type this mod itself adds to the Rebar Gun's
// mAllowedAmmoClasses in RegisterPulseRebarAsRebarGunAmmo() - alongside the four vanilla types.
const FNobeliskDescriptorInfo RebarGunDescriptors[] = {
	{ TEXT("/Game/FactoryGame/Resource/Parts/SpikedRebar/Desc_SpikedRebar.Desc_SpikedRebar_C"), TEXT("StandardCapacity") },
	{ TEXT("/Game/FactoryGame/Equipment/RebarGun/Ammo/Desc_Rebar_Explosive.Desc_Rebar_Explosive_C"), TEXT("ExplosiveCapacity") },
	{ TEXT("/Game/FactoryGame/Equipment/RebarGun/Ammo/Desc_Rebar_Spreadshot.Desc_Rebar_Spreadshot_C"), TEXT("SpreadshotCapacity") },
	{ TEXT("/Game/FactoryGame/Equipment/RebarGun/Ammo/Desc_Rebar_Stunshot.Desc_Rebar_Stunshot_C"), TEXT("StunshotCapacity") },
	{ TEXT("/NobeliskPlus/Ammo/Desc_Rebar_Pulse.Desc_Rebar_Pulse_C"), TEXT("PulseCapacity") },
};

UConfigPropertyFloat* FindMultiplierProperty(UConfigPropertySection* section, const TCHAR* name)
{
	return section != nullptr ? Cast<UConfigPropertyFloat>(section->SectionProperties.FindRef(name)) : nullptr;
}

// mAutomaticallyReload is protected on AFGWeapon (see mAllowedAmmoClasses above for why
// that means reflection instead of direct member access). Applied both to a Blueprint CDO,
// which is what weapons spawned later copy from, and to already-spawned instances, which
// would otherwise keep the value they copied at spawn time - see ApplyAutoReloadTarget.
void SetWeaponAutomaticallyReload(AFGWeapon& weapon, bool bAutomaticallyReload)
{
	FBoolProperty* autoReloadProp = FReflectionHelper::FindPropertyChecked<FBoolProperty>(AFGWeapon::StaticClass(), TEXT("mAutomaticallyReload"));
	autoReloadProp->SetPropertyValue_InContainer(&weapon, bAutomaticallyReload);
}

// mMagazineSize is protected on UFGAmmoType (GetMagSize() reads it through the ammo class's
// own CDO at fire/reload time - see FGWeapon.h - so patching it here is enough, unlike the
// weapon-side numbers above which live on a per-instance RadialForceComponent template).
FIntProperty* GetMagazineSizeProperty()
{
	static FIntProperty* prop = FReflectionHelper::FindPropertyChecked<FIntProperty>(UFGAmmoType::StaticClass(), TEXT("mMagazineSize"));
	return prop;
}

int32 GetMagazineSize(const UFGAmmoType& ammoCdo)
{
	return GetMagazineSizeProperty()->GetPropertyValue_InContainer(&ammoCdo);
}

void SetMagazineSize(UFGAmmoType& ammoCdo, int32 magazineSize)
{
	GetMagazineSizeProperty()->SetPropertyValue_InContainer(&ammoCdo, magazineSize);
}

// RadialForce is a Blueprint Simple-Construction-Script component (added via the
// Components panel), not a component the native base class constructs in its own
// constructor. SCS components are NOT attached to a Blueprint class's CDO - they only
// get instanced onto actual spawned actors via the construction script - so
// cdo->FindComponentByClass<URadialForceComponent>() always silently returns null here,
// regardless of the component visibly existing in the Blueprint. What every spawned
// instance actually copies its property values from is the SCS node's ComponentTemplate,
// which is what needs patching instead.
URadialForceComponent* FindRadialForceComponentTemplate(UClass* actorClass)
{
	for (UClass* currentClass = actorClass; currentClass != nullptr; currentClass = currentClass->GetSuperClass())
	{
		const UBlueprintGeneratedClass* blueprintClass = Cast<UBlueprintGeneratedClass>(currentClass);
		if (blueprintClass == nullptr || blueprintClass->SimpleConstructionScript == nullptr)
			continue;

		for (USCS_Node* node : blueprintClass->SimpleConstructionScript->GetAllNodes())
		{
			if (URadialForceComponent* component = Cast<URadialForceComponent>(node->ComponentTemplate))
			{
				return component;
			}
		}
	}
	return nullptr;
}

// ObjectTypesToAffect is protected on URadialForceComponent, so - like mAllowedAmmoClasses
// on AFGWeapon - it's reached through reflection rather than direct member access.
// TEnumAsByte<EObjectTypeQuery> surfaces as a plain FByteProperty array element.
void EnsureObjectTypesToAffectIncludesPlayer(URadialForceComponent& radialForce, const FString& projectilePath)
{
	FArrayProperty* objectTypesProp = FReflectionHelper::FindPropertyChecked<FArrayProperty>(URadialForceComponent::StaticClass(), TEXT("ObjectTypesToAffect"));
	FByteProperty* elementProp = CastField<FByteProperty>(objectTypesProp->Inner);
	check(elementProp != nullptr);

	FScriptArrayHelper arrayHelper(objectTypesProp, objectTypesProp->ContainerPtrToValuePtr<void>(&radialForce));

	// Checking Num() == 0 to decide "unconfigured" was wrong: Unreal only serializes a
	// property into the .uasset when it differs from its class default, and
	// URadialForceComponent's own C++ default for this array is apparently already
	// non-empty (it survived unnoticed because nothing here ever logged the values it
	// found) - so BP_Rebar_Explosive silently inheriting that default, never having
	// explicitly added Pawn, looked identical to "already configured" under a Num()>0
	// check. Check for Pawn specifically instead.
	const uint8 pawnByte = static_cast<uint8>(UEngineTypes::ConvertToObjectType(ECC_Pawn));
	for (int32 index = 0; index < arrayHelper.Num(); ++index)
	{
		if (elementProp->GetPropertyValue(arrayHelper.GetRawPtr(index)) == pawnByte)
		{
			UE_LOG(LogNobeliskPlus, Log, TEXT("%s's RadialForceComponent already affects Pawns (%d object type(s) configured)."), *projectilePath, arrayHelper.Num());
			return;
		}
	}

	// The Radius/Impulse/Force values only decide how strong the push is; whether it
	// affects a player at all is gated separately by ObjectTypesToAffect, which
	// BP_Rebar_Explosive's component never had Pawn added to (it's set up for a bit of
	// debris knock on impact, not for pushing the player - unlike BP_NobeliskShockwave's,
	// which already includes Pawn). Match Nobelisk's set so the Pulse Rebar behaves the
	// same way, replacing whatever was there rather than appending to avoid duplicates.
	const EObjectTypeQuery objectTypes[] = {
		UEngineTypes::ConvertToObjectType(ECC_WorldDynamic),
		UEngineTypes::ConvertToObjectType(ECC_Pawn),
		UEngineTypes::ConvertToObjectType(ECC_PhysicsBody),
		UEngineTypes::ConvertToObjectType(ECC_Vehicle),
		UEngineTypes::ConvertToObjectType(ECC_Destructible),
	};
	const int32 previousNum = arrayHelper.Num();
	arrayHelper.Resize(UE_ARRAY_COUNT(objectTypes));
	for (int32 index = 0; index < UE_ARRAY_COUNT(objectTypes); ++index)
	{
		elementProp->SetPropertyValue(arrayHelper.GetRawPtr(index), static_cast<uint8>(objectTypes[index]));
	}

	UE_LOG(LogNobeliskPlus, Log, TEXT("%s's RadialForceComponent did not affect Pawns (had %d object type(s) configured, none of them Pawn); replaced with Pawn/PhysicsBody/Vehicle/Destructible/WorldDynamic."), *projectilePath, previousNum);
}

// Scans actorClass's full native+Blueprint class hierarchy for any object property whose
// declared type derives from assetBaseClass, and returns the first non-null value found on
// the class default object. Used to let the Pulse Rebar reuse whatever impact effect the
// vanilla Pulse Nobelisk uses, without needing to know its exact property name - useful
// since BP_NobeliskShockwave's own Blueprint graph has no VFX references at all, meaning the
// effect is set up natively in code this project does not have source access to; if it lives
// on a reflected UPROPERTY anywhere in the chain (native or Blueprint), this finds it
// regardless. Checked for both UParticleSystem (legacy Cascade) and UNiagaraSystem, since
// this project has both integrated and it isn't obvious from the outside which one a given
// effect actually uses.
UObject* FindAssetPropertyValue(UClass* actorClass, UClass* assetBaseClass)
{
	UObject* cdo = actorClass->GetDefaultObject();
	for (UClass* cls = actorClass; cls != nullptr; cls = cls->GetSuperClass())
	{
		for (TFieldIterator<FObjectProperty> propIt(cls, EFieldIteratorFlags::ExcludeSuper); propIt; ++propIt)
		{
			FObjectProperty* prop = *propIt;
			if (prop->PropertyClass == nullptr || !prop->PropertyClass->IsChildOf(assetBaseClass))
				continue;

			if (UObject* value = prop->GetObjectPropertyValue_InContainer(cdo))
			{
				UE_LOG(LogNobeliskPlus, Log, TEXT("Found %s property %s::%s = %s"), *assetBaseClass->GetName(), *cls->GetName(), *prop->GetName(), *value->GetPathName());
				return value;
			}
		}
	}
	return nullptr;
}

// The Pulse Nobelisk's explosion visual is not stored in any property - not on the projectile
// CDO, not on its damage types, not on the ammo descriptor (all checked). It is spawned by
// Blueprint graph logic (AFGProjectile::PlayExplosionEffects is a BlueprintImplementableEvent),
// which reflection cannot see because an asset referenced only from graph bytecode never
// appears as a UPROPERTY value.
//
// It IS recorded as a package dependency though, so ask the asset registry what
// BP_NobeliskShockwave's package pulls in and pick the VFX asset out of that. Walks one level
// of indirection as well, since the effect is often referenced via an intermediate Blueprint.
// Prefers a name containing "shock" when several candidates exist, so we get the shockwave
// rather than some unrelated smoke/debris system that happens to be referenced too.
// Assets referenced only from Blueprint *graph* nodes are not property values, so no
// reflection scan of the CDO can ever see them. They are however recorded in the compiled
// function bytecode, and UStruct::ScriptAndPropertyObjectReferences mirrors exactly those
// object references (it exists so GC can keep them alive). Scanning it finds assets used by
// graph logic such as AFGProjectile::PlayExplosionEffects.
//
// This works in a shipped build, unlike asset-registry dependency queries: cooked builds
// strip dependency data from the runtime asset registry, so that approach silently returns
// nothing in the actual game even though it works fine in the editor.
// Same bytecode trick as FindEffectAssetInBytecode, but matched on a class NAME so we can
// look for AkAudioEvent without taking a hard dependency on the AkAudio module.
//
// A destructive projectile's bytecode references MULTIPLE events of the same class - e.g.
// AFGDestructiveProjectile fires a different Wwise event per destroyed-surface type (rock,
// foliage, metal...) as well as its own detonation. Taking the first match found
// "Play_Nobelisk_WorldDestruction_SmallRock" instead of the actual explosion sound. Collect
// every match instead and pick by name.
UObject* FindBytecodeReferenceByClassName(UClass* blueprintClass, const TCHAR* wantedClassName, TFunctionRef<bool(const FString&)> isPreferredName)
{
	TArray<UObject*> candidates;

	for (UClass* cls = blueprintClass; cls != nullptr; cls = cls->GetSuperClass())
	{
		auto scan = [wantedClassName, &candidates](const UStruct* target)
		{
			if (target == nullptr)
				return;
			for (const TObjectPtr<UObject>& reference : target->ScriptAndPropertyObjectReferences)
			{
				UObject* object = reference.Get();
				if (object != nullptr && object->GetClass()->GetName() == wantedClassName)
					candidates.AddUnique(object);
			}
		};

		scan(cls);
		for (TFieldIterator<UFunction> functionIt(cls, EFieldIteratorFlags::ExcludeSuper); functionIt; ++functionIt)
		{
			scan(*functionIt);
		}
	}

	if (candidates.IsEmpty())
		return nullptr;

	for (const UObject* candidate : candidates)
	{
		UE_LOG(LogNobeliskPlus, Log, TEXT("Bytecode-referenced %s candidate: %s"), wantedClassName, *candidate->GetPathName());
	}

	UObject** preferred = candidates.FindByPredicate([&isPreferredName](const UObject* candidate)
	{
		return isPreferredName(candidate->GetName());
	});
	return preferred != nullptr ? *preferred : candidates[0];
}

UObject* FindEffectAssetInBytecode(UClass* blueprintClass)
{
	TArray<UObject*> candidates;
	// Diagnostic: if no effect turns up, this tells us what the bytecode DOES reference,
	// rather than leaving us to guess why the scan came back empty.
	TSet<FString> referencedClassNames;
	int32 totalReferences = 0;

	auto scanStruct = [&candidates, &referencedClassNames, &totalReferences](const UStruct* target)
	{
		if (target == nullptr)
			return;
		for (const TObjectPtr<UObject>& reference : target->ScriptAndPropertyObjectReferences)
		{
			UObject* object = reference.Get();
			if (object == nullptr)
				continue;
			++totalReferences;
			referencedClassNames.Add(object->GetClass()->GetName());
			if (object->IsA<UNiagaraSystem>() || object->IsA<UParticleSystem>())
			{
				candidates.AddUnique(object);
			}
		}
	};

	for (UClass* cls = blueprintClass; cls != nullptr; cls = cls->GetSuperClass())
	{
		scanStruct(cls);
		for (TFieldIterator<UFunction> functionIt(cls, EFieldIteratorFlags::ExcludeSuper); functionIt; ++functionIt)
		{
			scanStruct(*functionIt);
		}
	}

	if (candidates.IsEmpty())
	{
		UE_LOG(LogNobeliskPlus, Log, TEXT("Bytecode scan of %s found %d object reference(s) but no particle/Niagara system. Referenced types were: %s"),
			*blueprintClass->GetName(), totalReferences, *FString::Join(referencedClassNames.Array(), TEXT(", ")));
		return nullptr;
	}

	for (const UObject* candidate : candidates)
	{
		UE_LOG(LogNobeliskPlus, Log, TEXT("Bytecode-referenced effect candidate: %s (%s)"), *candidate->GetPathName(), *candidate->GetClass()->GetName());
	}

	UObject** preferred = candidates.FindByPredicate([](const UObject* candidate)
	{
		return candidate->GetName().Contains(TEXT("shock"));
	});
	return preferred != nullptr ? *preferred : candidates[0];
}

UObject* FindEffectAssetViaDependencies(const TCHAR* rootPackageName)
{
	const FAssetRegistryModule& assetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const IAssetRegistry& assetRegistry = assetRegistryModule.Get();

	const FTopLevelAssetPath niagaraClassPath = UNiagaraSystem::StaticClass()->GetClassPathName();
	const FTopLevelAssetPath particleClassPath = UParticleSystem::StaticClass()->GetClassPathName();

	TSet<FName> visited;
	TArray<FName> frontier;
	frontier.Add(FName(rootPackageName));

	TArray<FAssetData> candidates;

	constexpr int32 maxDepth = 2;
	for (int32 depth = 0; depth < maxDepth && frontier.Num() > 0; ++depth)
	{
		TArray<FName> nextFrontier;
		for (const FName& packageName : frontier)
		{
			bool alreadyVisited = false;
			visited.Add(packageName, &alreadyVisited);
			if (alreadyVisited)
				continue;

			TArray<FName> dependencies;
			assetRegistry.GetDependencies(packageName, dependencies);
			for (const FName& dependency : dependencies)
			{
				TArray<FAssetData> assets;
				assetRegistry.GetAssetsByPackageName(dependency, assets);
				for (const FAssetData& asset : assets)
				{
					if (asset.AssetClassPath == niagaraClassPath || asset.AssetClassPath == particleClassPath)
					{
						candidates.Add(asset);
					}
				}
				nextFrontier.Add(dependency);
			}
		}
		frontier = MoveTemp(nextFrontier);
	}

	if (candidates.IsEmpty())
		return nullptr;

	for (const FAssetData& candidate : candidates)
	{
		UE_LOG(LogNobeliskPlus, Log, TEXT("Candidate impact effect: %s (%s)"), *candidate.GetSoftObjectPath().ToString(), *candidate.AssetClassPath.ToString());
	}

	const FAssetData* chosen = candidates.FindByPredicate([](const FAssetData& asset)
	{
		return asset.AssetName.ToString().Contains(TEXT("shock"));
	});
	if (chosen == nullptr)
	{
		chosen = &candidates[0];
	}

	return chosen->GetAsset();
}

// Replaces every entry of the descriptor's magazine material arrays. mMagazineMeshMaterials
// is TArray<FSkeletalMaterial> (a struct wrapper, so we reach into its MaterialInterface
// member) while mMagazineMeshMaterials1p is a plain object array - both are protected, hence
// reflection.
void SetMagazineMaterials(UClass* ammoClass, UObject* ammoCdo)
{
	UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, PulseRebarMagazineMaterialPath);
	UMaterialInterface* material1p = LoadObject<UMaterialInterface>(nullptr, PulseRebarMagazineMaterial1pPath);
	if (material == nullptr && material1p == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Warning, TEXT("Could not load the Pulse Rebar magazine materials; the loaded round will keep the explosive rebar's red look."));
		return;
	}

	if (FArrayProperty* skeletalMaterialsProp = CastField<FArrayProperty>(ammoClass->FindPropertyByName(TEXT("mMagazineMeshMaterials"))))
	{
		if (FStructProperty* elementProp = CastField<FStructProperty>(skeletalMaterialsProp->Inner))
		{
			if (FObjectProperty* materialProp = CastField<FObjectProperty>(elementProp->Struct->FindPropertyByName(TEXT("MaterialInterface"))))
			{
				FScriptArrayHelper helper(skeletalMaterialsProp, skeletalMaterialsProp->ContainerPtrToValuePtr<void>(ammoCdo));
				for (int32 index = 0; index < helper.Num(); ++index)
				{
					materialProp->SetObjectPropertyValue(materialProp->ContainerPtrToValuePtr<void>(helper.GetRawPtr(index)), material);
				}
				UE_LOG(LogNobeliskPlus, Log, TEXT("Set %d magazine material slot(s) to %s."), helper.Num(), PulseRebarMagazineMaterialPath);
			}
		}
	}

	if (FArrayProperty* materials1pProp = CastField<FArrayProperty>(ammoClass->FindPropertyByName(TEXT("mMagazineMeshMaterials1p"))))
	{
		if (FObjectProperty* elementProp = CastField<FObjectProperty>(materials1pProp->Inner))
		{
			FScriptArrayHelper helper(materials1pProp, materials1pProp->ContainerPtrToValuePtr<void>(ammoCdo));
			for (int32 index = 0; index < helper.Num(); ++index)
			{
				elementProp->SetObjectPropertyValue(helper.GetRawPtr(index), material1p);
			}
			UE_LOG(LogNobeliskPlus, Log, TEXT("Set %d first-person magazine material slot(s) to %s."), helper.Num(), PulseRebarMagazineMaterial1pPath);
		}
	}
}

void EnsureProjectileMeshAssigned(UClass* actorClass, const FString& projectilePath)
{
	// ANobeliskPlusPulseRebarProjectile creates RebarMesh as a native default subobject, so
	// unlike a Blueprint SCS component it genuinely exists on the CDO and can be found here.
	AActor* cdo = Cast<AActor>(actorClass->GetDefaultObject());
	UStaticMeshComponent* meshComponent = cdo != nullptr ? cdo->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	if (meshComponent == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Warning, TEXT("%s has no StaticMeshComponent; the round will be invisible in flight."), *projectilePath);
		return;
	}

	if (meshComponent->GetStaticMesh() != nullptr)
		return;

	UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, RebarProjectileMeshPath);
	if (mesh == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load %s; the Pulse Rebar will stay invisible in flight."), RebarProjectileMeshPath);
		return;
	}

	meshComponent->SetStaticMesh(mesh);
	UE_LOG(LogNobeliskPlus, Log, TEXT("Assigned %s to %s's %s so the round is visible in flight."),
		*mesh->GetName(), *projectilePath, *meshComponent->GetName());
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
	RebarTarget.bEnsurePlayerCanBePushed = true;
	RebarTarget.bDrivesNativePulse = true;

	FNobeliskPlusAutoReloadTarget& detonatorAutoReload = AutoReloadTargets.AddDefaulted_GetRef();
	detonatorAutoReload.EquipmentPath = NobeliskDetonatorEquipmentPath;
	detonatorAutoReload.ConfigSectionName = TEXT("Detonator");

	FNobeliskPlusAutoReloadTarget& rebarGunAutoReload = AutoReloadTargets.AddDefaulted_GetRef();
	rebarGunAutoReload.EquipmentPath = RebarGunEquipmentPath;
	rebarGunAutoReload.ConfigSectionName = TEXT("RebarGun");

	FNobeliskPlusCapacityGroup& detonatorCapacity = CapacityGroups.AddDefaulted_GetRef();
	detonatorCapacity.ConfigSectionName = TEXT("Detonator");

	FNobeliskPlusCapacityGroup& rebarGunCapacity = CapacityGroups.AddDefaulted_GetRef();
	rebarGunCapacity.ConfigSectionName = TEXT("RebarGun");
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
	RegisterPulseRebarAsRebarGunAmmo();
	ClearPulseRebarAmmoDamage();

	for (FNobeliskPlusAutoReloadTarget& target : AutoReloadTargets)
	{
		SetUpAutoReloadTarget(target, configRoot);
	}
	OnAutoReloadConfigChanged();

	// Indices match the order the groups were added to CapacityGroups in the constructor:
	// Detonator, then Rebar Gun.
	check(CapacityGroups.Num() == 2);
	SetUpCapacityGroup(CapacityGroups[0], configRoot, NobeliskDetonatorDescriptors);
	SetUpCapacityGroup(CapacityGroups[1], configRoot, RebarGunDescriptors);
	OnCapacityConfigChanged();

	{
		UClass* nobeliskClass = StaticLoadClass(AActor::StaticClass(), nullptr, PulseNobeliskProjectilePath);
		ANobeliskPlusPulseRebarProjectile::ImpactEffect = nullptr;
		ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect = nullptr;
		if (nobeliskClass != nullptr)
		{
			ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect = Cast<UNiagaraSystem>(FindAssetPropertyValue(nobeliskClass, UNiagaraSystem::StaticClass()));
			if (ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect == nullptr)
			{
				ANobeliskPlusPulseRebarProjectile::ImpactEffect = Cast<UParticleSystem>(FindAssetPropertyValue(nobeliskClass, UParticleSystem::StaticClass()));
			}
		}

		// The property scan only sees assets held in UPROPERTY fields. The Nobelisk's explosion
		// visual is spawned from Blueprint graph logic instead, so fall back to asking the asset
		// registry what its package actually depends on.
		if (ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect == nullptr && ANobeliskPlusPulseRebarProjectile::ImpactEffect == nullptr)
		{
			UObject* effect = nobeliskClass != nullptr ? FindEffectAssetInBytecode(nobeliskClass) : nullptr;
			if (effect == nullptr)
			{
				effect = FindEffectAssetViaDependencies(TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/BP_NobeliskShockwave"));
			}
			ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect = Cast<UNiagaraSystem>(effect);
			ANobeliskPlusPulseRebarProjectile::ImpactEffect = Cast<UParticleSystem>(effect);
			if (effect != nullptr)
			{
				UE_LOG(LogNobeliskPlus, Log, TEXT("Found impact effect via package dependencies: %s"), *effect->GetPathName());
			}
		}

		if (ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect != nullptr || ANobeliskPlusPulseRebarProjectile::ImpactEffect != nullptr)
		{
			UE_LOG(LogNobeliskPlus, Log, TEXT("Pulse Rebar will reuse the Pulse Nobelisk's impact %s system."), ANobeliskPlusPulseRebarProjectile::ImpactNiagaraEffect != nullptr ? TEXT("Niagara") : TEXT("particle"));
		}
		else
		{
			UE_LOG(LogNobeliskPlus, Log, TEXT("Could not find any particle/Niagara system asset reference on the Pulse Nobelisk's class hierarchy - it is likely spawned natively in code this project does not have source for. The Pulse Rebar's impact will have no VFX."));
		}
	}
}

void UNobeliskPlusGameInstanceModule::RegisterPulseRebarAsRebarGunAmmo() const
{
	UClass* rebarGunClass = StaticLoadClass(AFGWeapon::StaticClass(), nullptr, RebarGunEquipmentPath);
	UClass* pulseRebarDescriptorClass = StaticLoadClass(UObject::StaticClass(), nullptr, PulseRebarAmmoDescriptorPath);
	if (rebarGunClass == nullptr || pulseRebarDescriptorClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the Rebar Gun and/or the Pulse Rebar ammo descriptor; the Pulse Rebar won't be loadable into the Rebar Gun."));
		return;
	}

	// mAllowedAmmoClasses is protected on AFGWeapon, so it's appended to via reflection
	// rather than direct member access.
	AFGWeapon* rebarGunCdo = rebarGunClass->GetDefaultObject<AFGWeapon>();
	FArrayProperty* allowedAmmoProp = FReflectionHelper::FindPropertyChecked<FArrayProperty>(AFGWeapon::StaticClass(), TEXT("mAllowedAmmoClasses"));
	FClassProperty* elementProp = CastField<FClassProperty>(allowedAmmoProp->Inner);
	check(elementProp != nullptr);

	FScriptArrayHelper arrayHelper(allowedAmmoProp, allowedAmmoProp->ContainerPtrToValuePtr<void>(rebarGunCdo));
	for (int32 index = 0; index < arrayHelper.Num(); ++index)
	{
		if (elementProp->GetObjectPropertyValue(arrayHelper.GetRawPtr(index)) == pulseRebarDescriptorClass)
		{
			return; // already present (e.g. a second call within the same process)
		}
	}

	const int32 newIndex = arrayHelper.AddValue();
	elementProp->SetObjectPropertyValue(arrayHelper.GetRawPtr(newIndex), pulseRebarDescriptorClass);

	UE_LOG(LogNobeliskPlus, Log, TEXT("Added the Pulse Rebar to the Rebar Gun's allowed ammo classes."));
}

void UNobeliskPlusGameInstanceModule::SetUpShockwaveTarget(FNobeliskPlusShockwaveTarget& target, UConfigPropertySection* configRoot)
{
	UClass* projectileClass = StaticLoadClass(AActor::StaticClass(), nullptr, *target.ProjectilePath);
	if (projectileClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the projectile class at %s; its shockwave was not amplified."), *target.ProjectilePath);
		return;
	}

	target.RadialForce = FindRadialForceComponentTemplate(projectileClass);
	if (target.RadialForce == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("%s no longer has a RadialForceComponent; its shockwave was not amplified."), *target.ProjectilePath);
		return;
	}

	target.BaseRadius = target.RadialForce->Radius;
	target.BaseImpulseStrength = target.RadialForce->ImpulseStrength;
	target.BaseForceStrength = target.RadialForce->ForceStrength;
	CDOEdits.Add(target.RadialForce);

	if (target.bEnsurePlayerCanBePushed)
	{
		EnsureObjectTypesToAffectIncludesPlayer(*target.RadialForce, target.ProjectilePath);
	}

	UConfigPropertySection* section = configRoot != nullptr
		? Cast<UConfigPropertySection>(configRoot->SectionProperties.FindRef(target.ConfigSectionName))
		: nullptr;
	target.RadiusMultiplierProperty = FindMultiplierProperty(section, TEXT("RadiusMultiplier"));
	target.ImpulseMultiplierProperty = FindMultiplierProperty(section, TEXT("ImpulseMultiplier"));
	target.ForceMultiplierProperty = FindMultiplierProperty(section, TEXT("ForceMultiplier"));

	// Muzzle velocity lives on the projectile's own movement component; the Pulse Rebar's
	// ammo descriptor leaves mInitialProjectileSpeedOverride unset, so nothing downstream
	// overwrites what we set here at fire time.
	if (target.bDrivesNativePulse)
	{
		if (AFGProjectile* projectileCdo = Cast<AFGProjectile>(projectileClass->GetDefaultObject()))
		{
			target.ProjectileMovement = projectileCdo->GetProjectileMovement();
			if (target.ProjectileMovement != nullptr)
			{
				target.BaseInitialSpeed = target.ProjectileMovement->InitialSpeed;
				target.BaseMaxSpeed = target.ProjectileMovement->MaxSpeed;
				CDOEdits.Add(target.ProjectileMovement);
			}
			else
			{
				UE_LOG(LogNobeliskPlus, Warning, TEXT("%s has no projectile movement component; its speed will not be adjustable."), *target.ProjectilePath);
			}
		}
		target.SpeedMultiplierProperty = FindMultiplierProperty(section, TEXT("SpeedMultiplier"));
		EnsureProjectileMeshAssigned(projectileClass, target.ProjectilePath);
	}

	for (UConfigPropertyFloat* property : {target.RadiusMultiplierProperty, target.ImpulseMultiplierProperty, target.ForceMultiplierProperty, target.SpeedMultiplierProperty})
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

	if (target.ProjectileMovement != nullptr)
	{
		const float speedMultiplier = target.SpeedMultiplierProperty ? target.SpeedMultiplierProperty->Value : 1.0f;
		target.ProjectileMovement->InitialSpeed = target.BaseInitialSpeed * speedMultiplier;
		// MaxSpeed clamps velocity, so it has to rise with InitialSpeed or the multiplier is
		// silently capped. A base MaxSpeed of 0 means "no limit" - leave that alone.
		target.ProjectileMovement->MaxSpeed = target.BaseMaxSpeed > 0.0f ? target.BaseMaxSpeed * speedMultiplier : 0.0f;

		UE_LOG(LogNobeliskPlus, Log, TEXT("Applying projectile speed for %s: initial %.0f -> %.0f, max %.0f -> %.0f"),
			*target.ProjectilePath,
			target.BaseInitialSpeed, target.ProjectileMovement->InitialSpeed,
			target.BaseMaxSpeed, target.ProjectileMovement->MaxSpeed);
	}

	if (target.bDrivesNativePulse)
	{
		// The Blueprint's RadialForceComponent is left configured above for physics debris,
		// but the push players actually feel comes from ANobeliskPlusPulseRebarProjectile.
		// Base launch velocity is its own number because LaunchCharacter takes cm/s, not an
		// impulse - the component's impulse figure would be meaningless there.
		constexpr float baseLaunchVelocity = 1200.0f;
		ANobeliskPlusPulseRebarProjectile::PulseRadius = target.BaseRadius * radiusMultiplier;
		ANobeliskPlusPulseRebarProjectile::PulseLaunchVelocity = baseLaunchVelocity * impulseMultiplier;
		ANobeliskPlusPulseRebarProjectile::PulsePhysicsImpulse = target.BaseImpulseStrength * impulseMultiplier;
	}
}

void UNobeliskPlusGameInstanceModule::ClearPulseRebarAmmoDamage() const
{
	// Damage does NOT come from the projectile Blueprint. AFGProjectile's own
	// mDamageTypesOnImpact/mDamageTypesAtEndOfLife are empty on BP_Rebar_Explosive too -
	// clearing them on BP_Rebar_Pulse was always a no-op. The damage types actually live on
	// the AMMO DESCRIPTOR (UFGAmmoType::mDamageTypesOnImpact and
	// UFGAmmoTypeProjectile::mDamageTypesAtEndOfLife) and are pushed onto each spawned
	// projectile at fire time via SetupProjectile/SetImpactDamageTypes.
	//
	// Do NOT empty those arrays: a UFGDamageType also carries mImpactAudioEvent and
	// mImpactParticle, so deleting the entries silently removes the impact SOUND and VFX
	// along with the damage. Zero mDamageAmount on each instance instead - the round then
	// hits for nothing while still looking and sounding like an impact.
	UClass* ammoClass = StaticLoadClass(UObject::StaticClass(), nullptr, PulseRebarAmmoDescriptorPath);
	if (ammoClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the Pulse Rebar ammo descriptor; it will still deal damage."));
		return;
	}

	UObject* ammoCdo = ammoClass->GetDefaultObject();
	SetMagazineMaterials(ammoClass, ammoCdo);

	UObject* nobeliskImpactAudio = nullptr;
	if (UClass* nobeliskClass = StaticLoadClass(AActor::StaticClass(), nullptr, PulseNobeliskProjectilePath))
	{
		nobeliskImpactAudio = FindBytecodeReferenceByClassName(nobeliskClass, TEXT("AkAudioEvent"), [](const FString& name)
		{
			return name.Contains(TEXT("Shockwave")) || name.Contains(TEXT("Detonat")) || name.Contains(TEXT("Explo"));
		});
	}
	if (nobeliskImpactAudio == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Warning, TEXT("Could not find a Wwise event in the Pulse Nobelisk's bytecode; the Pulse Rebar keeps the explosive rebar's impact sound."));
	}

	for (const TCHAR* propertyName : {TEXT("mDamageTypesOnImpact"), TEXT("mDamageTypesAtEndOfLife")})
	{
		FArrayProperty* damageTypesProp = CastField<FArrayProperty>(ammoClass->FindPropertyByName(propertyName));
		if (damageTypesProp == nullptr)
		{
			UE_LOG(LogNobeliskPlus, Warning, TEXT("Pulse Rebar ammo descriptor has no %s property; skipping it."), propertyName);
			continue;
		}

		FObjectProperty* elementProp = CastField<FObjectProperty>(damageTypesProp->Inner);
		if (elementProp == nullptr)
			continue;

		FScriptArrayHelper arrayHelper(damageTypesProp, damageTypesProp->ContainerPtrToValuePtr<void>(ammoCdo));
		for (int32 index = 0; index < arrayHelper.Num(); ++index)
		{
			UFGDamageType* damageType = Cast<UFGDamageType>(elementProp->GetObjectPropertyValue(arrayHelper.GetRawPtr(index)));
			if (damageType == nullptr)
				continue;

			const float previousDamage = damageType->mDamageAmount;
			damageType->mDamageAmount = 0.0f;
			UE_LOG(LogNobeliskPlus, Log, TEXT("Pulse Rebar %s[%d] (%s): damage %.1f -> 0 (impact audio/VFX left intact)."),
				propertyName, index, *damageType->GetClass()->GetName(), previousDamage);

			// The inherited audio is the explosive rebar's bang. Point it at whatever Wwise
			// event the Pulse Nobelisk's own Blueprint posts, so the round sounds like a
			// shockwave rather than an explosion. mImpactAudioEvent is a TSoftObjectPtr, so
			// it is written through reflection as a soft path - that also avoids needing the
			// AkAudio module just to name the type.
			if (nobeliskImpactAudio != nullptr)
			{
				if (FSoftObjectProperty* audioProp = CastField<FSoftObjectProperty>(
					damageType->GetClass()->FindPropertyByName(TEXT("mImpactAudioEvent"))))
				{
					FSoftObjectPtr newValue(nobeliskImpactAudio);
					audioProp->SetPropertyValue_InContainer(damageType, newValue);
					UE_LOG(LogNobeliskPlus, Log, TEXT("  impact audio -> %s"), *nobeliskImpactAudio->GetPathName());
				}
			}
		}
	}
}

void UNobeliskPlusGameInstanceModule::SetUpAutoReloadTarget(FNobeliskPlusAutoReloadTarget& target, UConfigPropertySection* configRoot)
{
	UClass* weaponClass = StaticLoadClass(AFGWeapon::StaticClass(), nullptr, *target.EquipmentPath);
	if (weaponClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the weapon equipment class at %s; its auto-reload option won't do anything."), *target.EquipmentPath);
		return;
	}

	target.WeaponCdo = Cast<AFGWeapon>(weaponClass->GetDefaultObject());
	if (target.WeaponCdo == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("%s's CDO is not an AFGWeapon; its auto-reload option won't do anything."), *target.EquipmentPath);
		return;
	}

	UConfigPropertySection* weaponSection = configRoot != nullptr
		? Cast<UConfigPropertySection>(configRoot->SectionProperties.FindRef(target.ConfigSectionName))
		: nullptr;
	target.AutoReloadProperty = weaponSection != nullptr
		? Cast<UConfigPropertyBool>(weaponSection->SectionProperties.FindRef(TEXT("AutoReload")))
		: nullptr;
	if (target.AutoReloadProperty != nullptr)
	{
		target.AutoReloadProperty->OnPropertyValueChanged.AddDynamic(this, &UNobeliskPlusGameInstanceModule::OnAutoReloadConfigChanged);
	}
}

void UNobeliskPlusGameInstanceModule::OnAutoReloadConfigChanged()
{
	for (const FNobeliskPlusAutoReloadTarget& target : AutoReloadTargets)
	{
		ApplyAutoReloadTarget(target);
	}
}

void UNobeliskPlusGameInstanceModule::ApplyAutoReloadTarget(const FNobeliskPlusAutoReloadTarget& target) const
{
	if (target.WeaponCdo == nullptr)
		return;

	const bool bAutoReload = target.AutoReloadProperty != nullptr ? target.AutoReloadProperty->Value : false;
	SetWeaponAutomaticallyReload(*target.WeaponCdo, bAutoReload);

	// Patching the CDO alone only affects weapons spawned from here on. mAutomaticallyReload
	// is EditDefaultsOnly, so an equipment instance copies it once at spawn time and then
	// keeps that value - which is why toggling this setting mid-game appeared to do nothing
	// for a weapon the player was already holding, even though the log said "enabled".
	// Refreshing live instances is also what lets a plain AFGWeapon's own native auto-reload
	// work, since that reads the instance's copy rather than the class default.
	UClass* weaponClass = target.WeaponCdo->GetClass();
	int32 instancesUpdated = 0;
	for (TObjectIterator<AFGWeapon> weaponIt; weaponIt; ++weaponIt)
	{
		AFGWeapon* weapon = *weaponIt;
		if (weapon == nullptr || weapon->GetClass() != weaponClass || weapon->HasAnyFlags(RF_ClassDefaultObject))
			continue;

		SetWeaponAutomaticallyReload(*weapon, bAutoReload);
		++instancesUpdated;
	}

	UE_LOG(LogNobeliskPlus, Log, TEXT("%s auto-reload: %s (%d live instance(s) updated)"),
		*target.EquipmentPath, bAutoReload ? TEXT("enabled") : TEXT("disabled"), instancesUpdated);
}

void UNobeliskPlusGameInstanceModule::SetUpCapacityGroup(FNobeliskPlusCapacityGroup& group, UConfigPropertySection* configRoot, TArrayView<const FNobeliskDescriptorInfo> descriptors)
{
	UConfigPropertySection* weaponSection = configRoot != nullptr
		? Cast<UConfigPropertySection>(configRoot->SectionProperties.FindRef(group.ConfigSectionName))
		: nullptr;
	UConfigPropertySection* capacitySection = weaponSection != nullptr
		? Cast<UConfigPropertySection>(weaponSection->SectionProperties.FindRef(TEXT("Capacity")))
		: nullptr;

	group.GeneralCapacityProperty = capacitySection != nullptr
		? Cast<UConfigPropertyInteger>(capacitySection->SectionProperties.FindRef(TEXT("GeneralCapacity")))
		: nullptr;
	if (group.GeneralCapacityProperty != nullptr)
	{
		group.GeneralCapacityProperty->OnPropertyValueChanged.AddDynamic(this, &UNobeliskPlusGameInstanceModule::OnCapacityConfigChanged);
	}

	group.Targets.Reset();
	for (const FNobeliskDescriptorInfo& descriptor : descriptors)
	{
		UClass* ammoClass = StaticLoadClass(UFGAmmoType::StaticClass(), nullptr, descriptor.Path);
		if (ammoClass == nullptr)
		{
			UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load ammo descriptor %s; its capacity won't be configurable."), descriptor.Path);
			continue;
		}

		FNobeliskPlusAmmoCapacityTarget target;
		target.DescriptorPath = descriptor.Path;
		target.ConfigPropertyName = descriptor.ConfigPropertyName;
		target.AmmoCdo = Cast<UFGAmmoType>(ammoClass->GetDefaultObject());
		if (target.AmmoCdo == nullptr)
		{
			UE_LOG(LogNobeliskPlus, Error, TEXT("%s's CDO is not a UFGAmmoType; its capacity won't be configurable."), descriptor.Path);
			continue;
		}
		target.BaseMagazineSize = GetMagazineSize(*target.AmmoCdo);

		target.SpecificCapacityProperty = capacitySection != nullptr
			? Cast<UConfigPropertyInteger>(capacitySection->SectionProperties.FindRef(descriptor.ConfigPropertyName))
			: nullptr;
		if (target.SpecificCapacityProperty != nullptr)
		{
			target.SpecificCapacityProperty->OnPropertyValueChanged.AddDynamic(this, &UNobeliskPlusGameInstanceModule::OnCapacityConfigChanged);
		}

		group.Targets.Add(target);
	}
}

void UNobeliskPlusGameInstanceModule::OnCapacityConfigChanged()
{
	for (const FNobeliskPlusCapacityGroup& group : CapacityGroups)
	{
		const int32 generalCapacity = group.GeneralCapacityProperty != nullptr ? group.GeneralCapacityProperty->Value : 0;
		for (const FNobeliskPlusAmmoCapacityTarget& target : group.Targets)
		{
			ApplyCapacityTarget(target, generalCapacity);
		}
	}
}

void UNobeliskPlusGameInstanceModule::ApplyCapacityTarget(const FNobeliskPlusAmmoCapacityTarget& target, int32 generalCapacity) const
{
	if (target.AmmoCdo == nullptr)
		return;

	const int32 specificCapacity = target.SpecificCapacityProperty != nullptr ? target.SpecificCapacityProperty->Value : 0;
	const int32 effectiveCapacity = specificCapacity > 0 ? specificCapacity : (generalCapacity > 0 ? generalCapacity : target.BaseMagazineSize);

	SetMagazineSize(*target.AmmoCdo, effectiveCapacity);
	UE_LOG(LogNobeliskPlus, Log, TEXT("%s magazine capacity: %d -> %d"), *target.DescriptorPath, target.BaseMagazineSize, effectiveCapacity);
}
