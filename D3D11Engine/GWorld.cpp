#include "pch.h"
#include "GWorld.h"
#include "zCBspTree.h"
#include "GVobObject.h"
#include "GothicAPI.h"
#include "GVisual.h"
#include "GWorldMesh.h"
#include "zCVisual.h"
#include "zCProgMeshProto.h"
#include "GStaticMeshVisual.h"
#include "GSkeletalMeshVisual.h"
#include "zCModel.h"
#include "GModelVisual.h"
#include "GMorphMeshVisual.h"
#include "GParticleFXVisual.h"
#include "GDecalVisual.h"
#include "zCTexture.h"

GWorld::GWorld(void)
{
	WorldMesh = NULL;
}


GWorld::~GWorld(void)
{
	delete WorldMesh;

	Toolbox::DeleteElements<GVobObject*>(StaticVobList);
	Toolbox::DeleteElements<GVobObject*>(DynamicVobList);
	Toolbox::DeleteElements<zCVob*, GVobObject*>(VobMap);
	Toolbox::DeleteElements<zCBspBase*, BspNodeInfo*>(BspMap);
}

/** Calculates the level of this node and all subnodes */
int BspNodeInfo::CalculateLevels()
{
	if(OriginalNode->IsLeaf())
		NumLevels = 0;
	else
	{
		int f=0, b=0;

		if(Front)
			f = Front->CalculateLevels()+1;

		if(Back)
			b = Back->CalculateLevels()+1;

		NumLevels = std::max(f,b);
	}

	return NumLevels;		
}

/** Called on render */
void GWorld::DrawWorld()
{
	WorldMesh->DrawMesh();

	if(Engine::GAPI->GetRendererState()->RendererSettings.DrawVOBs)
	{
		

		// Tell all visuals that the frame has started
		BeginVisualFrame();
		
		// Draw visible BSP-Nodes
		BspDrawnVobs.clear();
		DrawBspTreeVobs(BspMap[Engine::GAPI->GetLoadedWorldInfo()->BspTree->GetRootNode()]);

		// Reset state on the drawn vobs
		ResetBspDrawnVobs();

		// Tell all visuals that the frame has ended
		EndVisualFrame();
	}

	Engine::GraphicsEngine->FlushRenderQueue();
}

/** Begins the frame on visuals */
void GWorld::BeginVisualFrame()
{
	for(auto it = VisualMap.begin();it!= VisualMap.end();it++)
		(*it).second->OnBeginDraw();
}

/** Ends the frame on visuals */
void GWorld::EndVisualFrame()
{
	for(auto it = VisualMap.begin();it!= VisualMap.end();it++)
		(*it).second->OnEndDraw();
}

/** Draws all vobs in the given node */
void GWorld::PrepareBspNodeVobPipelineStates(BspNodeInfo* node, std::set<GVisual*>& drawnVisuals, std::vector<GVobObject*>& drawnVobs)
{


	// Draw normal vobs
	for(auto it = node->Vobs.begin(); it != node->Vobs.end(); it++)
	{
		GVobObject* vob = (*it);	

		if(!vob->IsAlreadyCollectedFromTree())
		{
			vob->DrawVob();	
			vob->SetCollectedFromTreeSearch();
			drawnVisuals.insert(vob->GetVisual());
			drawnVobs.push_back(vob);
		}
	}

	// Draw small vobs
	for(auto it = node->SmallVobs.begin(); it != node->SmallVobs.end(); it++)
	{
		GVobObject* vob = (*it);

		if(!vob->IsAlreadyCollectedFromTree())
		{
			vob->DrawVob();	
			vob->SetCollectedFromTreeSearch();
			drawnVisuals.insert(vob->GetVisual());
			drawnVobs.push_back(vob);
		}
	}

	// Draw indoor vobs
	for(auto it = node->IndoorVobs.begin(); it != node->IndoorVobs.end(); it++)
	{
		GVobObject* vob = (*it);

		if(!vob->IsAlreadyCollectedFromTree())
		{
			vob->DrawVob();
			vob->SetCollectedFromTreeSearch();
			drawnVisuals.insert(vob->GetVisual());
			drawnVobs.push_back(vob);
		}
	}

	// Draw indoor lights
	/*for(auto it = node->IndoorLights.begin(); it != node->IndoorLights.end(); it++)
	{
		GVobObject* vob = (*it);
		vob->DrawVob();
		drawnVisuals.push_back(vob->GetVisual());
	}

	// Draw normal lights
	for(auto it = node->Lights.begin(); it != node->Lights.end(); it++)
	{
		GVobObject* vob = (*it);
		vob->DrawVob();
		drawnVisuals.push_back(vob->GetVisual());
	}*/

	
}

/** Draws all visible vobs in the tree */
void GWorld::PrepareBspTreePipelineStates(BspNodeInfo* base, std::set<GVisual*>& drawnVisuals, std::vector<GVobObject*>& drawnVobs)
{
	if(!base)
		return;

	if(base->OriginalNode->IsLeaf())
	{
		// Just draw everything here
		PrepareBspNodeVobPipelineStates(base, drawnVisuals, drawnVobs);
	}else
	{
		// We are just a node, continue with the tree
		PrepareBspTreePipelineStates(base->Front, drawnVisuals, drawnVobs);
		PrepareBspTreePipelineStates(base->Back, drawnVisuals, drawnVobs);
	}
}

/** Prepares all visible vobs in the tree */
void GWorld::PrepareBspTreeVobs(BspNodeInfo* base)
{
	if(!base) // Little shortcut for better readability later
		return;

	// Clear queue, because we only want the states of this particular node
	Engine::GraphicsEngine->ClearRenderingQueue();

	// Force cachein of textures
	// TODO: This might be bad, because it would load every single texture in the world
	//		 Probably only precompute the nodes if they get visible for the first time
	//		 Also make sure the textures won't be cached out, or if they are, take the state with them!
	//		 Another option would be to give the zCTexture-Pointer into the states to make sure their textures are loaded
	zCTextureCacheHack::ForceCacheIn = true;

	std::set<GVisual*> drawnVisuals;
	std::vector<GVobObject*> drawnVobs;

	// Init instancing-buffers
	BeginVisualFrame();

	// Create the pipeline states
	PrepareBspTreePipelineStates(base, drawnVisuals, drawnVobs);

	// Unmap instancingbuffers again
	EndVisualFrame();

	// Switch buffers for all drawn visuals, because we need to keep them now
	// Leaving them would cause them to be overwritten by other nodes drawing these visuals
	// Therefore we also need to delete all the states ourselfs
	for(auto it = drawnVisuals.begin(); it != drawnVisuals.end(); it++)
	{
		(*it)->SwitchInstanceSpecificResources();
	}

	for(auto it = drawnVobs.begin(); it != drawnVobs.end(); it++)
	{
		(*it)->ResetTreeSearchState();
	}

	// Save the statelist we have in this node
	// TODO: Recreate if one object was moved to dynamic list!
	base->StaticObjectPipelineStates = Engine::GraphicsEngine->GetRenderQueue();

	// Clear again, so we don't mess up further frames
	Engine::GraphicsEngine->ClearRenderingQueue();

	// Disable hack to not mess up further drawing
	zCTextureCacheHack::ForceCacheIn = false;


	// Continue with the tree, if possible
	PrepareBspTreeVobs(base->Front);
	PrepareBspTreeVobs(base->Back);
}


/** Draws all visible vobs in the tree */
void GWorld::DrawBspTreeVobs(BspNodeInfo* base)
{
	if(!base) // Little shortcut for better readability later
		return;

	float dist = Toolbox::ComputePointAABBDistance(Engine::GAPI->GetCameraPosition(), base->OriginalNode->BBox3D.Min, base->OriginalNode->BBox3D.Max);
	//if(dist > 20000)
	//	return;

	// Frustum-check-functions from gothic need this
	if(zCCamera::GetCamera())
		zCCamera::GetCamera()->Activate();

	// Draw everything visible in the bsp-tree
	DrawPreparedPipelineStatesRec(base, base->OriginalNode->BBox3D, 63);
	return;

	if(base->OriginalNode->IsLeaf())
	{
		// Just draw everything here
		// DrawBspNodeVobs(base, dist);

		DrawPreparedPipelineStates(base);
	}else
	{
		// We are just a node, continue with the tree
		DrawBspTreeVobs(base->Front);
		DrawBspTreeVobs(base->Back);
	}
}

void GWorld::DrawPreparedPipelineStatesRec(BspNodeInfo* base, zTBBox3D boxCell, int clipFlags)
{
	if(!base)
		return;

	D3DXVECTOR3 camPos = Engine::GAPI->GetCameraPosition();
	float vobOutdoorDist = Engine::GAPI->GetRendererState()->RendererSettings.OutdoorVobDrawRadius;
	zTCam_ClipType nodeClip = ZTCAM_CLIPTYPE_OUT;

	while(base && base->OriginalNode)
	{
		if (clipFlags>0) 
		{
			float yMaxWorld = Engine::GAPI->GetLoadedWorldInfo()->BspTree->GetRootNode()->BBox3D.Max.y;

			zTBBox3D nodeBox = base->OriginalNode->BBox3D;
			//float nodeYMax = std::min(yMaxWorld, Engine::GAPI->GetCameraPosition().y);
			//nodeYMax = std::max(nodeYMax, base->OriginalNode->BBox3D.Max.y);
			//nodeBox.Max.y = nodeYMax;

			float dist = Toolbox::ComputePointAABBDistance(camPos, base->OriginalNode->BBox3D.Min, base->OriginalNode->BBox3D.Max);
			if(dist < vobOutdoorDist)
			{
				nodeClip = zCCamera::GetCamera()->BBox3DInFrustum(nodeBox, clipFlags);
				
				if (nodeClip==ZTCAM_CLIPTYPE_OUT) 
					return; // Nothig to see here. Discard this node and the subtree}
			}else
			{
				// Too far
				return;
			}
		}

		// Is the whole node visible? If so, draw everything inside it
		// Also draw it if this is a crossing leaf, or the node isn't too huge
		// Gothics BSP-Trees can get really stupid and huge sometimes, we don't need all the 
		// precision here, it hurts instancing.
		if(nodeClip == ZTCAM_CLIPTYPE_IN || (nodeClip == ZTCAM_CLIPTYPE_CROSSING && base->NumLevels < 6))
		{
			//Engine::GraphicsEngine->GetLineRenderer()->AddAABBMinMax(base->OriginalNode->BBox3D.Min, base->OriginalNode->BBox3D.Max, D3DXVECTOR4(0,1,0,1));

			DrawPreparedPipelineStates(base);

			// This also draws all sub-nodes if there are any, so just return here
			// We are not calling the draw-method for each leaf, because we
			// can much bigger instancing buffers for big nodes
			return;
		}

		// If this node is only crossing, we may can break the visible nodes further down
		if(nodeClip == ZTCAM_CLIPTYPE_CROSSING)
		{
			//Engine::GraphicsEngine->GetLineRenderer()->AddAABBMinMax(base->OriginalNode->BBox3D.Min, base->OriginalNode->BBox3D.Max, D3DXVECTOR4(1,1,0,1));

			zCBspNode* node = (zCBspNode *)base->OriginalNode;

			int	planeAxis = node->PlaneSignbits;

			boxCell.Min.y	= node->BBox3D.Min.y;
			boxCell.Max.y	= node->BBox3D.Min.y;

			zTBBox3D tmpbox = boxCell;
			if (D3DXVec3Dot(&node->Plane.Normal, &camPos) > node->Plane.Distance)
			{ 
				if(node->Front) 
				{
					((float *)&tmpbox.Min)[planeAxis] = node->Plane.Distance;

					// Continue the tree
					DrawPreparedPipelineStatesRec(base->Front, tmpbox, clipFlags);
				}

				((float *)&boxCell.Max)[planeAxis] = node->Plane.Distance;

				// Try back-node
				base = base->Back;

			} else 
			{
				if (node->Back ) 
				{
					((float *)&tmpbox.Max)[planeAxis] = node->Plane.Distance;
					
					// Continue the tree
					DrawPreparedPipelineStatesRec(base->Back, tmpbox, clipFlags);
				}

				((float *)&boxCell.Min)[planeAxis] = node->Plane.Distance;

				// Try front-node
				base = base->Front;
			}
		}
	}
}

/** Draws the prepared pipelinestates of a BSP-Node */
void GWorld::DrawPreparedPipelineStates(BspNodeInfo* node)
{
	for(auto it = node->StaticObjectPipelineStates.begin(); it != node->StaticObjectPipelineStates.end(); it++)
	{
		PipelineState* s = (*it)->AssociatedState;

		if(!s->BaseState.BSPSkipState)
		{
			if(s->BaseState.TextureIDs[0] != 0xFFFF)
			{
				// Add state to renderqueue
				Engine::GraphicsEngine->PushPipelineState(s);
			}

			s->BaseState.BSPSkipState = true;
		}
	}
}

/** Draws all vobs in the given node */
void GWorld::DrawBspNodeVobs(BspNodeInfo* node, float nodeDistance)
{
	const float dist = FLT_MAX;

	// Draw normal vobs
	for(auto it = node->Vobs.begin(); it != node->Vobs.end(); it++)
	{
		GVobObject* vob = (*it);

		if(!vob->IsAlreadyCollectedFromTree() /*&& D3DXVec3Length(&(vob->GetSourceVob()->GetPositionWorld() - Engine::GAPI->GetCameraPosition())) < dist*/)
		{
			// Just draw // TODO: Implement culling
			vob->DrawVob();
			vob->SetCollectedFromTreeSearch();
			BspDrawnVobs.push_back(vob);
		}
	}

	// Draw small vobs
	for(auto it = node->SmallVobs.begin(); it != node->SmallVobs.end(); it++)
	{
		GVobObject* vob = (*it);

		if(!vob->IsAlreadyCollectedFromTree() /*&& D3DXVec3Length(&(vob->GetSourceVob()->GetPositionWorld() - Engine::GAPI->GetCameraPosition())) < dist*/)
		{
			// Just draw // TODO: Implement culling
			vob->DrawVob();
			vob->SetCollectedFromTreeSearch();
			BspDrawnVobs.push_back(vob);
		}
	}

	// Draw indoor vobs
	for(auto it = node->IndoorVobs.begin(); it != node->IndoorVobs.end(); it++)
	{
		GVobObject* vob = (*it);

		if(!vob->IsAlreadyCollectedFromTree() /*&& D3DXVec3Length(&(vob->GetSourceVob()->GetPositionWorld() - Engine::GAPI->GetCameraPosition())) < dist*/)
		{
			// Just draw // TODO: Implement culling
			vob->DrawVob();
			vob->SetCollectedFromTreeSearch();
			BspDrawnVobs.push_back(vob);
		}
	}

	// Draw indoor lights
	for(auto it = node->IndoorLights.begin(); it != node->IndoorLights.end(); it++)
	{
		GVobObject* vob = (*it);

		if(!vob->IsAlreadyCollectedFromTree() /*&& D3DXVec3Length(&(vob->GetSourceVob()->GetPositionWorld() - Engine::GAPI->GetCameraPosition())) < dist*/)
		{
			// Just draw // TODO: Implement culling
			vob->DrawVob();
			vob->SetCollectedFromTreeSearch();
			BspDrawnVobs.push_back(vob);
		}
	}

	// Draw normal lights
	for(auto it = node->Lights.begin(); it != node->Lights.end(); it++)
	{
		GVobObject* vob = (*it);

		if(!vob->IsAlreadyCollectedFromTree() /*&& D3DXVec3Length(&(vob->GetSourceVob()->GetPositionWorld() - Engine::GAPI->GetCameraPosition())) < dist*/)
		{
			// Just draw // TODO: Implement culling
			vob->DrawVob();
			vob->SetCollectedFromTreeSearch();
			BspDrawnVobs.push_back(vob);
		}
	}
}

/** Extracts all vobs from the bsp-tree, resets old tree */
void GWorld::BuildBSPTree(zCBspTree* bspTree)
{
	// Reset tree
	BspMap.clear();

	// Recursivly go through the tree
	BuildBspTreeHelper(bspTree->GetRootNode());

	// Prepare rendering
	PrepareBspTreeVobs(BspMap[Engine::GAPI->GetLoadedWorldInfo()->BspTree->GetRootNode()]);

	// Calculate levels of the tree
	BspMap[Engine::GAPI->GetLoadedWorldInfo()->BspTree->GetRootNode()]->CalculateLevels();
}

/** (Re)loads the world mesh */
void GWorld::ExtractWorldMesh(zCBspTree* bspTree)
{
	// Clear old mesh
	delete WorldMesh;

	// Load new one
	WorldMesh = new GWorldMesh(bspTree);
}

/** Helper function for going through the bsp-tree */
void GWorld::BuildBspTreeHelper(zCBspBase* base)
{
	if(!base)
		return;

	// Check if this is already in
	BspNodeInfo* bvi = BspMap[base];

	// Don't ever do this twice!
	if(bvi)
		return;

	// Put into cache
	bvi = new BspNodeInfo;
	BspMap[base] = bvi;

	bvi->OriginalNode = base;

	if(base->IsLeaf())
	{
		zCBspLeaf* leaf = (zCBspLeaf *)base;
				
		bvi->Front = NULL;
		bvi->Back = NULL;
		
		for(int i=0;i<leaf->LeafVobList.NumInArray;i++)
		{
			// Get the vob info for this one
			if(VobMap.find(leaf->LeafVobList.Array[i]) != VobMap.end())
			{
				GVobObject* v = VobMap[leaf->LeafVobList.Array[i]];

				if(v)
				{				
					float vobSmallSize = Engine::GAPI->GetRendererState()->RendererSettings.SmallVobSize;

					if(v->GetSourceVob()->IsIndoorVob())
					{
						// Only add once
						if(std::find(bvi->IndoorVobs.begin(), bvi->IndoorVobs.end(), v) == bvi->IndoorVobs.end())
						{
							v->AddParentBSPNode(bvi);

							bvi->IndoorVobs.push_back(v);	
						}
					}
					else if(v->GetVisual()->GetVisualSize() < vobSmallSize)
					{
						// Only add once
						if(std::find(bvi->SmallVobs.begin(), bvi->SmallVobs.end(), v) == bvi->SmallVobs.end())
						{
							v->AddParentBSPNode(bvi);

							bvi->SmallVobs.push_back(v);
						}
					}
					else
					{
						// Only add once
						if(std::find(bvi->Vobs.begin(), bvi->Vobs.end(), v) == bvi->Vobs.end())
						{
							v->AddParentBSPNode(bvi);

							bvi->Vobs.push_back(v);
						}
					}
				}
			}
		}
	}else
	{
		zCBspNode* node = (zCBspNode *)base;

		bvi->OriginalNode = base;

		BuildBspTreeHelper(node->Front);
		BuildBspTreeHelper(node->Back);

		// Save front and back to this
		bvi->Front = BspMap[node->Front];
		bvi->Back = BspMap[node->Back];
	}
}

/** Called when the game wanted to add a vob to the world */
void GWorld::AddVob(zCVob* vob, zCWorld* world, bool forceDynamic)
{
	// Don't need a vob without visual
	if(!vob->GetVisual())
		return;

	GVobObject* v = VobMap[vob];
	GVisual* visual = GetVisualFrom(vob->GetVisual());

	if(v) // Check if we already have this vob
		return; // We do, skip it!

	// Check if this visual was already loaded
	if(!visual)
	{
		// No, load it now.
		visual = CreateVisualFrom(vob->GetVisual());

		// Failed to load the visual?
		if(!visual)
		{
			LogError() << "Failed to load visual: " << vob->GetVisual()->GetObjectName();
			return; 
		}

		// Register it in our map
		VisualMap[vob->GetVisual()] = visual;
	}


	
	// If we got here, v must be NULL.
	v = new GVobObject(vob, visual);

	// Insert into vob-map
	VobMap[vob] = v;
	RegisterSingleVob(v, forceDynamic);
}

/** Creates the right type of visual from the source */
GVisual* GWorld::CreateVisualFrom(zCVisual* sourceVisual)
{
	switch(sourceVisual->GetVisualType())
	{
	case zCVisual::VT_PROGMESHPROTO:
		return new GStaticMeshVisual(sourceVisual);
		break;

	case zCVisual::VT_MODEL:
		return new GModelVisual(sourceVisual);
		break;

	case zCVisual::VT_MORPHMESH:
		return new GMorphMeshVisual(sourceVisual);
		break;

	case zCVisual::VT_PARTICLE_FX:
		return new GParticleFXVisual(sourceVisual);
		break;

	case zCVisual::VT_DECAL:
		return new GDecalVisual(sourceVisual);
		break;

	default:
		return NULL;
	}
}

/** Returns the matching GVisual from the given zCVisual */
GVisual* GWorld::GetVisualFrom(zCVisual* sourceVisual)
{
	return VisualMap[sourceVisual];
}

/** Creates a model-proto form the given object */
GSkeletalMeshVisual* GWorld::CreateModelProtoFrom(zCModelMeshLib* sourceProto)
{
	GSkeletalMeshVisual* proto = new GSkeletalMeshVisual(sourceProto);
	ModelProtoMap[sourceProto] = proto;

	return proto;
}

/** Returns the matching GSkeletalMeshVisual from the given zCModelPrototype */
GSkeletalMeshVisual* GWorld::GetModelProtoFrom(zCModelMeshLib* sourceProto)
{
	return ModelProtoMap[sourceProto];
}

/** Registers a single vob in all needed data structures */
void GWorld::RegisterSingleVob(GVobObject* vob, bool forceDynamic)
{
	if(forceDynamic)
	{
		// Put into dynamic list if wanted
		DynamicVobList.push_back(vob);
	}else
	{
		StaticVobList.push_back(vob);
	}

	vob->SetDynamic(forceDynamic);
}

/** Unregisters a single vob from all data structures */
void GWorld::UnregisterSingleVob(GVobObject* vob)
{
	auto dit = std::find(DynamicVobList.begin(), DynamicVobList.end(), vob);
	auto sit = std::find(StaticVobList.begin(), StaticVobList.end(), vob);

	// Remove from our static/dynamic vob-lists
	if(dit != DynamicVobList.end())
		DynamicVobList.erase(dit);

	if(sit != StaticVobList.end())
		StaticVobList.erase(sit);

	// Remove from BSP-Tree
	const std::vector<BspNodeInfo*>& parentNodes = vob->GetParentBSPNodes();
	for(auto it = parentNodes.begin(); it != parentNodes.end(); it++)
	{
		(*it)->RemoveVob(vob);
	}

	// Free memory
	delete vob;
}

/** Removes a vob from the world */
bool GWorld::RemoveVob(zCVob* vob, zCWorld* world)
{
	GVobObject* obj = VobMap[vob];

	if(obj) // Not registered here?
	{
		// Remove from voblist
		UnregisterSingleVob(obj);
	}

	// Remove from map (Still do it if obj is NULL)
	VobMap.erase(vob);

	return obj != NULL;
}

/** Resets all vobs in the BspDrawnVobs-Vector */
void GWorld::ResetBspDrawnVobs()
{
	for(auto it = BspDrawnVobs.begin(); it != BspDrawnVobs.end(); it++)
	{
		(*it)->ResetTreeSearchState();
	}
}