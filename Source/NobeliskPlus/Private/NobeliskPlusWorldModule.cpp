#include "NobeliskPlusWorldModule.h"

#include "FGResearchTree.h"
#include "FGResearchTreeNode.h"
#include "FGSchematic.h"
#include "AvailabilityDependencies/FGSchematicPurchasedDependency.h"
#include "NobeliskPlus.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr const TCHAR* QuartzResearchTreePath = TEXT("/Game/FactoryGame/Schematics/Research/BPD_ResearchTree_Quartz.BPD_ResearchTree_Quartz_C");
constexpr const TCHAR* PulseNobeliskSchematicPath = TEXT("/Game/FactoryGame/Schematics/Research/Quartz_RS/Research_Quartz_3_4.Research_Quartz_3_4_C");
constexpr const TCHAR* PulseRebarSchematicPath = TEXT("/NobeliskPlus/Schematic_PulseRebar.Schematic_PulseRebar_C");

// The Quartz tree's nodes are instances of the base game's BPD_ResearchTreeNode_C. Their
// layout/dependency data lives in a Blueprint-only struct member (mNodeDataStruct, of
// UserDefinedStruct type MAMTree_NodeData_Struct) with no native C++ declaration, so it can
// only be read or written through raw property reflection. UserDefinedStruct fields also get
// mangled, GUID-suffixed internal names (e.g. "Schematic_27_3663A4...") - only the *display*
// name shown in the editor is clean - so lookups below match on a name prefix rather than an
// exact name.
template <typename T>
T* FindPropertyByShortName(UStruct* Struct, const TCHAR* Name)
{
	for (FProperty* Property = Struct->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
	{
		if (Property->GetName().StartsWith(Name))
		{
			if (T* Casted = CastField<T>(Property))
			{
				return Casted;
			}
		}
	}
	return nullptr;
}

// Wraps a research tree node's mNodeDataStruct so its Schematic/Coordinates/Parents fields
// can be read and written without a native struct declaration to overlay on it.
struct FNodeDataStructAccessor
{
	void* NodeDataPtr = nullptr;
	UScriptStruct* NodeDataStructType = nullptr;

	static FNodeDataStructAccessor ForNode(UFGResearchTreeNode* Node)
	{
		FNodeDataStructAccessor Accessor;
		if (FStructProperty* NodeDataProp = FindPropertyByShortName<FStructProperty>(Node->GetClass(), TEXT("mNodeDataStruct")))
		{
			Accessor.NodeDataPtr = NodeDataProp->ContainerPtrToValuePtr<void>(Node);
			Accessor.NodeDataStructType = NodeDataProp->Struct;
		}
		return Accessor;
	}

	bool IsValid() const { return NodeDataPtr != nullptr && NodeDataStructType != nullptr; }

	void SetSchematic(UClass* SchematicClass) const
	{
		if (FClassProperty* SchematicProp = FindPropertyByShortName<FClassProperty>(NodeDataStructType, TEXT("Schematic")))
		{
			SchematicProp->SetObjectPropertyValue(SchematicProp->ContainerPtrToValuePtr<void>(NodeDataPtr), SchematicClass);
		}
	}

	// Address (and script struct type) of the Coordinates sub-struct - a small Blueprint
	// struct with two int members, X and Y.
	void* GetCoordinatesPtr(UScriptStruct** OutCoordStructType) const
	{
		FStructProperty* CoordProp = FindPropertyByShortName<FStructProperty>(NodeDataStructType, TEXT("Coordinates"));
		if (CoordProp == nullptr)
		{
			return nullptr;
		}
		if (OutCoordStructType != nullptr)
		{
			*OutCoordStructType = CoordProp->Struct;
		}
		return CoordProp->ContainerPtrToValuePtr<void>(NodeDataPtr);
	}

	void OffsetCoordinatesX(int32 DeltaX) const
	{
		UScriptStruct* CoordStructType = nullptr;
		void* CoordPtr = GetCoordinatesPtr(&CoordStructType);
		if (CoordPtr == nullptr)
		{
			return;
		}
		if (FIntProperty* XProp = FindPropertyByShortName<FIntProperty>(CoordStructType, TEXT("X")))
		{
			const int32 CurrentX = XProp->GetPropertyValue(XProp->ContainerPtrToValuePtr<void>(CoordPtr));
			XProp->SetPropertyValue(XProp->ContainerPtrToValuePtr<void>(CoordPtr), CurrentX + DeltaX);
		}
	}

	// Replaces the Parents array with a single entry equal to ParentNode's current
	// Coordinates - i.e. makes this node depend on / render after ParentNode.
	void SetSingleParent(const FNodeDataStructAccessor& ParentNode) const
	{
		UScriptStruct* ParentCoordType = nullptr;
		void* ParentCoordPtr = ParentNode.GetCoordinatesPtr(&ParentCoordType);
		FArrayProperty* ParentsProp = FindPropertyByShortName<FArrayProperty>(NodeDataStructType, TEXT("Parents"));
		if (ParentCoordPtr == nullptr || ParentsProp == nullptr)
		{
			return;
		}
		FScriptArrayHelper Helper(ParentsProp, ParentsProp->ContainerPtrToValuePtr<void>(NodeDataPtr));
		Helper.EmptyValues();
		Helper.AddValue();
		ParentCoordType->CopyScriptStruct(Helper.GetRawPtr(0), ParentCoordPtr);
	}

	// UnhiddenBy / NodesToUnhide are arrays of the same Coordinates struct as Parents. A node
	// stays hidden in the MAM until a node listed in its UnhiddenBy is researched, and
	// researching a node reveals the ones in its NodesToUnhide. Returns the array, or null if
	// the field is missing or isn't a Coordinates array.
	FArrayProperty* FindCoordinatesArray(const TCHAR* Name) const
	{
		UScriptStruct* CoordStructType = nullptr;
		GetCoordinatesPtr(&CoordStructType);
		FArrayProperty* ArrayProp = FindPropertyByShortName<FArrayProperty>(NodeDataStructType, Name);
		const FStructProperty* InnerProp = ArrayProp != nullptr ? CastField<FStructProperty>(ArrayProp->Inner) : nullptr;
		if (InnerProp == nullptr || InnerProp->Struct != CoordStructType)
		{
			UE_LOG(LogNobeliskPlus, Warning, TEXT("Research node data has no Coordinates array named %s; left as is."), Name);
			return nullptr;
		}
		return ArrayProp;
	}

	void SetCoordinatesArray(const TCHAR* Name, const FNodeDataStructAccessor* Entry) const
	{
		FArrayProperty* ArrayProp = FindCoordinatesArray(Name);
		if (ArrayProp == nullptr)
		{
			return;
		}
		FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(NodeDataPtr));
		Helper.EmptyValues();
		if (Entry != nullptr)
		{
			UScriptStruct* CoordStructType = nullptr;
			if (void* EntryCoordPtr = Entry->GetCoordinatesPtr(&CoordStructType))
			{
				Helper.AddValue();
				CoordStructType->CopyScriptStruct(Helper.GetRawPtr(0), EntryCoordPtr);
			}
		}
	}

	void AddToCoordinatesArray(const TCHAR* Name, const FNodeDataStructAccessor& Entry) const
	{
		FArrayProperty* ArrayProp = FindCoordinatesArray(Name);
		UScriptStruct* CoordStructType = nullptr;
		void* EntryCoordPtr = Entry.GetCoordinatesPtr(&CoordStructType);
		if (ArrayProp == nullptr || EntryCoordPtr == nullptr)
		{
			return;
		}
		FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(NodeDataPtr));
		const int32 Index = Helper.AddValue();
		CoordStructType->CopyScriptStruct(Helper.GetRawPtr(Index), EntryCoordPtr);
	}

	// ChildrenAndRoads maps child coordinates to the road drawn to them. The copy would draw
	// the Pulse Nobelisk's roads from the Pulse Rebar node, so empty it.
	void ClearChildrenAndRoads() const
	{
		if (FMapProperty* MapProp = FindPropertyByShortName<FMapProperty>(NodeDataStructType, TEXT("ChildrenAndRoads")))
		{
			FScriptMapHelper Helper(MapProp, MapProp->ContainerPtrToValuePtr<void>(NodeDataPtr));
			Helper.EmptyValues();
		}
	}
};

// Makes PulseRebarSchematic purchasable only once PulseNobeliskSchematic has been purchased.
// Copied from the Pulse Nobelisk's own dependency object, so the result is the same concrete
// UFGSchematicPurchasedDependency Blueprint vanilla research uses (the native class is abstract).
// mSchematicDependencies and mSchematics are protected, hence reflection.
void RequirePulseNobeliskForPulseRebar(UClass* PulseNobeliskSchematicClass, UClass* PulseRebarSchematicClass)
{
	FArrayProperty* DependenciesProp = CastField<FArrayProperty>(UFGSchematic::StaticClass()->FindPropertyByName(TEXT("mSchematicDependencies")));
	FObjectProperty* DependencyElementProp = DependenciesProp != nullptr ? CastField<FObjectProperty>(DependenciesProp->Inner) : nullptr;
	FArrayProperty* SchematicsProp = CastField<FArrayProperty>(UFGSchematicPurchasedDependency::StaticClass()->FindPropertyByName(TEXT("mSchematics")));
	FClassProperty* SchematicElementProp = SchematicsProp != nullptr ? CastField<FClassProperty>(SchematicsProp->Inner) : nullptr;
	if (DependencyElementProp == nullptr || SchematicElementProp == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not find the schematic dependency properties; the Pulse Rebar won't require the Pulse Nobelisk."));
		return;
	}

	UObject* PulseNobeliskCdo = PulseNobeliskSchematicClass->GetDefaultObject();
	UObject* PulseRebarCdo = PulseRebarSchematicClass->GetDefaultObject();
	FScriptArrayHelper NobeliskDependencies(DependenciesProp, DependenciesProp->ContainerPtrToValuePtr<void>(PulseNobeliskCdo));
	FScriptArrayHelper RebarDependencies(DependenciesProp, DependenciesProp->ContainerPtrToValuePtr<void>(PulseRebarCdo));

	UFGSchematicPurchasedDependency* Template = nullptr;
	for (int32 Index = 0; Index < NobeliskDependencies.Num() && Template == nullptr; ++Index)
	{
		Template = Cast<UFGSchematicPurchasedDependency>(DependencyElementProp->GetObjectPropertyValue(NobeliskDependencies.GetRawPtr(Index)));
	}
	if (Template == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("The Pulse Nobelisk schematic has no schematic-purchased dependency to copy; the Pulse Rebar won't require the Pulse Nobelisk."));
		return;
	}

	UFGSchematicPurchasedDependency* Dependency = DuplicateObject<UFGSchematicPurchasedDependency>(Template, PulseRebarCdo);
	FScriptArrayHelper Schematics(SchematicsProp, SchematicsProp->ContainerPtrToValuePtr<void>(Dependency));
	Schematics.EmptyValues();
	Schematics.AddValue();
	SchematicElementProp->SetObjectPropertyValue(Schematics.GetRawPtr(0), PulseNobeliskSchematicClass);

	// Replace rather than append: this runs on every world load, and the CDO lives for the
	// whole process.
	RebarDependencies.EmptyValues();
	RebarDependencies.AddValue();
	DependencyElementProp->SetObjectPropertyValue(RebarDependencies.GetRawPtr(0), Dependency);

	UE_LOG(LogNobeliskPlus, Log, TEXT("Pulse Rebar schematic now requires %s (dependency class %s)."),
		*PulseNobeliskSchematicClass->GetName(), *Dependency->GetClass()->GetName());
}
} // namespace

UNobeliskPlusWorldModule::UNobeliskPlusWorldModule()
{
	// Without this, FPluginModuleLoader::FindRootModulesOfType never discovers this
	// class and DispatchLifecycleEvent is never called on it at all.
	bRootModule = true;
}

void UNobeliskPlusWorldModule::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	if (phase == ELifecyclePhase::CONSTRUCTION)
	{
		AddPulseRebarNodeToQuartzTree();
	}

	Super::DispatchLifecycleEvent(phase);
}

void UNobeliskPlusWorldModule::AddPulseRebarNodeToQuartzTree()
{
	UClass* QuartzTreeClass = StaticLoadClass(UFGResearchTree::StaticClass(), nullptr, QuartzResearchTreePath);
	UClass* PulseNobeliskSchematicClass = StaticLoadClass(UFGSchematic::StaticClass(), nullptr, PulseNobeliskSchematicPath);
	UClass* PulseRebarSchematicClass = StaticLoadClass(UFGSchematic::StaticClass(), nullptr, PulseRebarSchematicPath);
	if (QuartzTreeClass == nullptr || PulseNobeliskSchematicClass == nullptr || PulseRebarSchematicClass == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not load the Quartz tree and/or the Pulse Nobelisk/Pulse Rebar schematics; the Pulse Rebar won't be researchable."));
		return;
	}

	RequirePulseNobeliskForPulseRebar(PulseNobeliskSchematicClass, PulseRebarSchematicClass);

	TArray<UFGResearchTreeNode*> Nodes = UFGResearchTree::GetNodes(QuartzTreeClass);

	// Guards against duplicate injection: the class default object we're patching persists
	// for the lifetime of the process, so a second world load (e.g. loading another save
	// without restarting the game) would otherwise re-run this and add the node again.
	for (const UFGResearchTreeNode* Node : Nodes)
	{
		if (Node != nullptr && Node->GetNodeSchematic() == PulseRebarSchematicClass)
		{
			return;
		}
	}

	UFGResearchTreeNode* PulseNobeliskNode = nullptr;
	for (UFGResearchTreeNode* Node : Nodes)
	{
		if (Node != nullptr && Node->GetNodeSchematic() == PulseNobeliskSchematicClass)
		{
			PulseNobeliskNode = Node;
			break;
		}
	}
	if (PulseNobeliskNode == nullptr)
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not find the Pulse Nobelisk node in the Quartz research tree; the Pulse Rebar won't be researchable."));
		return;
	}

	const FNodeDataStructAccessor PulseNobeliskAccessor = FNodeDataStructAccessor::ForNode(PulseNobeliskNode);
	if (!PulseNobeliskAccessor.IsValid())
	{
		UE_LOG(LogNobeliskPlus, Error, TEXT("Could not read the Pulse Nobelisk node's layout data; the Pulse Rebar won't be researchable."));
		return;
	}

	UFGResearchTreeNode* NewNode = DuplicateObject<UFGResearchTreeNode>(PulseNobeliskNode, PulseNobeliskNode->GetOuter());
	const FNodeDataStructAccessor NewNodeAccessor = FNodeDataStructAccessor::ForNode(NewNode);
	NewNodeAccessor.SetSingleParent(PulseNobeliskAccessor); // depend on the (still-unmodified) Pulse Nobelisk node
	NewNodeAccessor.SetSchematic(PulseRebarSchematicClass);
	NewNodeAccessor.OffsetCoordinatesX(1); // place one column after it

	// The duplicate also inherited the Pulse Nobelisk's visibility links, so it was revealed by
	// whatever reveals the Pulse Nobelisk instead of by the Pulse Nobelisk itself. Re-link it:
	// hidden until the Pulse Nobelisk is researched, and revealing nothing of its own.
	NewNodeAccessor.SetCoordinatesArray(TEXT("UnhiddenBy"), &PulseNobeliskAccessor);
	NewNodeAccessor.SetCoordinatesArray(TEXT("NodesToUnhide"), nullptr);
	NewNodeAccessor.ClearChildrenAndRoads();
	PulseNobeliskAccessor.AddToCoordinatesArray(TEXT("NodesToUnhide"), NewNodeAccessor);

	Nodes.Add(NewNode);
	UFGResearchTree::SetNodes(QuartzTreeClass, Nodes);

	UE_LOG(LogNobeliskPlus, Log, TEXT("Added the Pulse Rebar research node to the Quartz tree, after the Pulse Nobelisk node, unhidden by it."));
}
