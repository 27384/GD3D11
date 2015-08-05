#pragma once
#include "pch.h"
#include "WorldConverter.h"
#include "BaseGraphicsEngine.h"

/** Currently loaded world */
class zCPolygon;
struct MeshInfo;
class GVisual;
class zCBspBase;
class GVobObject;
class ThreadPool;
struct BspNodeInfo
{
	BspNodeInfo()
	{
		OriginalNode = NULL;
		Front = NULL;
		Back = NULL;
		NumLevels = 0;
		OcclusionInfo.VisibleLastFrame = false;
		OcclusionInfo.LastVisitedFrameID = 0;
		OcclusionInfo.QueryID = -1;
		OcclusionInfo.QueryInProgress = false;
		OcclusionInfo.LastCameraClipType = 0;

		OcclusionInfo.NodeMesh = NULL;

		FrustumFailCache = -1;
	}

	~BspNodeInfo()
	{
		delete OcclusionInfo.NodeMesh;

		for(auto it = StaticObjectPipelineStates.begin();it != StaticObjectPipelineStates.end();it++)
		{
			// These are all only valid for this particular node
			// so we need to delete them here
			delete (*it)->AssociatedState->BaseState.VertexBuffers[1]; // This should hold an instancingbuffer, which is only valid for this node if its in here
			delete (*it);
		}
	}

	bool IsEmpty()
	{
		return Vobs.empty() && IndoorVobs.empty() && SmallVobs.empty() && Lights.empty() && IndoorLights.empty();
	}

	void RemoveVob(GVobObject* vob)
	{
		// Erase from all lists, expensive!
		Toolbox::EraseByElement(Vobs, vob);
		Toolbox::EraseByElement(IndoorVobs, vob);
		Toolbox::EraseByElement(SmallVobs, vob);
		Toolbox::EraseByElement(Lights, vob);
		Toolbox::EraseByElement(IndoorLights, vob);

		Toolbox::EraseByElement(NonInstanceableVobs, vob);
		Toolbox::EraseByElement(NonInstanceableIndoorVobs, vob);
		Toolbox::EraseByElement(NonInstanceableSmallVobs, vob);
	}

	/** Calculates the level of this node and all subnodes */
	int CalculateLevels();

	/** Vob-lists for this node */
	std::vector<GVobObject *> Vobs;
	std::vector<GVobObject *> IndoorVobs;
	std::vector<GVobObject *> SmallVobs;
	std::vector<GVobObject *> Lights;
	std::vector<GVobObject *> IndoorLights;

	std::vector<GVobObject *> NonInstanceableVobs;
	std::vector<GVobObject *> NonInstanceableIndoorVobs;
	std::vector<GVobObject *> NonInstanceableSmallVobs;

	// This is filled in case we have loaded a custom worldmesh
	std::vector<zCPolygon *> NodePolygons;

	/** Pipeline-states for everything static in this Node */
	std::vector<PipelineState::PipelineSortItem*> StaticObjectPipelineStates;

	/** Cache for instancing-info, so we can copy it to the buffers in batches */
	std::vector<std::pair<GVisual*, std::pair<int*,std::vector<VobInstanceRemapInfo>>>> InstanceDataCacheVobs;
	std::vector<std::pair<GVisual*, std::pair<int*,std::vector<VobInstanceRemapInfo>>>> InstanceDataCacheSmallVobs;
	std::vector<std::pair<GVisual*, std::pair<int*,std::vector<VobInstanceRemapInfo>>>> InstanceDataCacheIndoorVobs;

	/** Num of sub-nodes this node has */
	unsigned int NumLevels;

	/** Index of the frustum-plane we failed at last time.
		-1 if visible. */
	int FrustumFailCache;

	/** Occlusion info for this node */
	struct OcclusionInfo_s
	{
		unsigned int LastVisitedFrameID;
		bool VisibleLastFrame;
		int QueryID;
		bool QueryInProgress;
		MeshInfo* NodeMesh;
		int LastCameraClipType;
	} OcclusionInfo;

	// Original bsp-node
	zCBspBase* OriginalNode;
	BspNodeInfo* Front;
	BspNodeInfo* Back;
};

class GVobObject;
class GWorldMesh;
class GSkeletalMeshVisual;
class zCModelPrototype;

struct BspInfo;
class zCVob;
class GWorld
{
public:
	GWorld(void);
	virtual ~GWorld(void);

	/** Called on render */
	virtual void DrawWorld();

	/** Called on render */
	virtual void DrawWorldThreaded();

	/** (Re)loads the world mesh */
	void ExtractWorldMesh(zCBspTree* bspTree);

	/** Extracts all vobs from the bsp-tree, resets old tree */
	void BuildBSPTree(zCBspTree* bspTree);

	/** Called when the game wanted to add a vob to the world */
	void AddVob(zCVob* vob, zCWorld* world, bool forceDynamic = false);

	/** Removes a vob from the world, returns false if it wasn't registered */
	bool RemoveVob(zCVob* vob, zCWorld* world);

	/** Called when a vob moved */
	void OnVobMoved(zCVob* vob);

	/** Creates the right type of visual from the source */
	GVisual* CreateVisualFrom(zCVisual* sourceVisual);

	/** Returns the matching GVisual from the given zCVisual */
	GVisual* GetVisualFrom(zCVisual* sourceVisual);

	/** Creates a model-proto form the given object */
	GSkeletalMeshVisual* CreateModelProtoFrom(zCModelMeshLib* sourceProto);

	/** Returns the matching GSkeletalMeshVisual from the given zCModelPrototype */
	GSkeletalMeshVisual* GetModelProtoFrom(zCModelMeshLib* sourceProto);

	/** Registers a vob instance at the given slot.
		Enter -1 for a new slot. If you do so, also fill instanceTypeOffset.
		Returns the used slot. */
	int RegisterVobInstance(int slot, const VobInstanceRemapInfo& instance, int* instanceTypeOffset = NULL);

	/** Builds the vob instancebuffer */
	void BuildVobInstanceRemapBuffer();

	/** Returns a pointer to the current instancing buffer */
	BaseVertexBuffer* GetVobInstanceBuffer(){return VobInstanceBuffer;}
	BaseVertexBuffer* GetVobInstanceRemapBuffer(){return VobInstanceRemapBuffer;}

protected:
	/** Renderproc for the worldmesh */
	static void DrawWorldMeshProc();

	/** Renderproc for the vobs */
	static void DrawVobsProc();

	/** Renderproc for the vobs */
	static void DrawDynamicVobsProc();
	
	/** Begins the frame on visuals */
	void BeginVisualFrame();

	/** Ends the frame on visuals */
	void EndVisualFrame();

	/** Registers a single vob in all needed data structures */
	void RegisterSingleVob(GVobObject* vob, bool forceDynamic = false);

	/** Unregisters a single vob from all data structures */
	void UnregisterSingleVob(GVobObject* vob);

	/** Helper function for going through the bsp-tree to collect all the vobs*/
	void BuildBspTreeHelper(zCBspBase* base);

	/** Draws all visible vobs in the tree */
	void DrawBspTreeVobs(BspNodeInfo* base, zTBBox3D boxCell, int clipFlags = 63);

	/** Draws all vobs in the given node */
	void DrawBspNodeVobs(BspNodeInfo* node, float nodeDistance = 0.0f);

	/** Draws all vobs in the given node */
	void PrepareBspNodeVobPipelineStates(BspNodeInfo* node);

	/** Draws all visible vobs in the tree */
	void PrepareBspTreePipelineStates(BspNodeInfo* base);

	/** Draws the prepared pipelinestates of a BSP-Node */
	void DrawPreparedPipelineStates(BspNodeInfo* node, float nodeDistance);
	void DrawPreparedPipelineStatesRec(BspNodeInfo* base, zTBBox3D boxCell, int clipFlags);

	/** Prepares all visible vobs in the tree */
	void PrepareBspTreeVobs(BspNodeInfo* base);

	/** Resets all vobs in the BspDrawnVobs-Vector */
	void ResetBspDrawnVobs();

	/** Draws all dynamic vobs */
	void DrawDynamicVobs();

	/** Map of original vobs to our counterparts */
	std::unordered_map<zCVob*, GVobObject*> VobMap;

	/** List of static vobs. These are also registered in the BSP-Tree. */
	std::vector<GVobObject*> StaticVobList;

	/** List of dynamic vobs, these are not registered in the BSP-Tree */
	std::vector<GVobObject*> DynamicVobList;

	/** The currently loaded world mesh */
	GWorldMesh* WorldMesh;

	/** Map of VobInfo-Lists for zCBspLeafs */
	std::unordered_map<zCBspBase *, BspNodeInfo*> BspMap;

	/** Map of Visuals and our counterparts */
	std::unordered_map<zCVisual *, GVisual *> VisualMap;

	/** Map of Model-Prototypes and our counterparts */
	std::unordered_map<zCModelMeshLib*, GSkeletalMeshVisual*> ModelProtoMap;

	/** List of vobs drawn from the last BuildBspTree-Call */
	std::vector<GVobObject *> BspDrawnVobs;
	std::vector<GVisual *> DrawnVisuals;

	/** List of registered vob instances for faster rendering */
	std::vector<std::pair<int*,std::vector<VobInstanceRemapInfo>>> RegisteredVobInstances;
	unsigned int NumRegisteredVobInstances;
	unsigned int CurrentVobInstanceSlot;
	BaseVertexBuffer* VobInstanceBuffer;
	BaseVertexBuffer* VobInstanceRemapBuffer;
	byte* MappedInstanceData;
};

