#include "NobeliskPlusWorldModule.h"

#include "FGResearchTree.h"
#include "FGResearchTreeNode.h"
#include "FGSchematic.h"
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
};
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

	Nodes.Add(NewNode);
	UFGResearchTree::SetNodes(QuartzTreeClass, Nodes);

	UE_LOG(LogNobeliskPlus, Log, TEXT("Added the Pulse Rebar research node to the Quartz tree, after the Pulse Nobelisk node."));
}
