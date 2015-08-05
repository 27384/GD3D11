#include "pch.h"
#include "GStaticMeshVisual.h"
#include "WorldConverter.h"
#include "Engine.h"
#include "BaseGraphicsEngine.h"
#include "BaseLineRenderer.h"
#include "GVobObject.h"
#include "zCVob.h"
#include "zCMaterial.h"
#include <stdio.h>
#include "GGame.h"
#include "GWorld.h"

const int INSTANCING_BUFFER_SIZE = sizeof(VobInstanceInfo) * 64; // TODO: This is too small! But higher values are too memory intensive!

GStaticMeshVisual::GStaticMeshVisual(zCVisual* sourceVisual) : GVisual(sourceVisual)
{
	// Extract the info from the visual in the games memory
	WorldConverter::Extract3DSMeshFromVisual2((zCProgMeshProto*)sourceVisual, &VisualInfo);

	// Set other variables to their initial value
	Instancing.NumRegisteredInstances = 0;
	VisualSize = VisualInfo.MeshSize;

	// Create pipelinestates and instancing buffer
	SwitchInstanceSpecificResources();

	Instancing.InstancingBufferData = NULL;
	Instancing.InstanceTypeIndex = -1;
}


GStaticMeshVisual::~GStaticMeshVisual(void)
{
	Toolbox::DeleteElements(PipelineStates);
	Toolbox::DeleteElements(ImmediatePipelineStates);

	delete Instancing.InstancingBuffer;
}

/** Switches the resources so we can have multiple states on this visual.
		The BSP-Tree needs to grab the instancing-buffers for this for example,
		and every node needs its own version */
void GStaticMeshVisual::SwitchInstanceSpecificResources()
{
	if(Instancing.NumRegisteredInstances != 0)
	{
		LogWarn() << "SwitchInstanceSpecificResources on mapped instancing buffer!";
		return;
	}

	// Create the instancing buffer for this visual
	// Someone else now needs to take care of deleting this buffer
	Engine::GraphicsEngine->CreateVertexBuffer(&Instancing.InstancingBuffer);

	// Init it
	Instancing.InstancingBuffer->Init(NULL, INSTANCING_BUFFER_SIZE, BaseVertexBuffer::B_VERTEXBUFFER, BaseVertexBuffer::U_DYNAMIC, BaseVertexBuffer::CA_WRITE);

	// Someone else needs to take care of deleting the memory of these now
	PipelineStates.clear();

	// We need as many pipeline states as we have textures in the mesh
	for(auto it = VisualInfo.Meshes.begin();it != VisualInfo.Meshes.end(); it++)
	{
		for(int i = 0;i<(*it).second.size();i++)
		{
			// Enter a new state for this submesh-part
			PipelineStates.push_back(Engine::GraphicsEngine->CreatePipelineState());

			PipelineStates.back()->BaseState.DrawCallType = PipelineState::DCT_DrawIndexedInstanced;

			if((*it).first->GetAlphaFunc() > 1)
				PipelineStates.back()->BaseState.TranspacenyMode = PipelineState::ETransparencyMode::TM_MASKED;

			Engine::GraphicsEngine->SetupPipelineForStage(STAGE_DRAW_WORLD, PipelineStates.back());
				
			PipelineStates.back()->BaseState.VertexBuffers[0] = (*it).second[i]->MeshVertexBuffer;
			PipelineStates.back()->BaseState.IndexBuffer = (*it).second[i]->MeshIndexBuffer;
			PipelineStates.back()->BaseState.NumIndices = (*it).second[i]->Indices.size();
			PipelineStates.back()->BaseState.NumVertices = (*it).second[i]->Vertices.size();


			// Huge safety-check to see if gothic didn't mess this up
			if((*it).first &&
				(*it).first->GetTexture() &&
				(*it).first->GetTexture()->GetSurface() &&
				(*it).first->GetTexture()->GetSurface()->GetEngineTexture())
				PipelineStates.back()->BaseState.TextureIDs[0] = (*it).first->GetTexture()->GetSurface()->GetEngineTexture()->GetID();

			

			// Enter API-Specific values into the state-object
			Engine::GraphicsEngine->FillPipelineStateObject(PipelineStates.back());
		}
	}

	// Now for the immediate states
	for(auto it = VisualInfo.Meshes.begin();it != VisualInfo.Meshes.end(); it++)
	{
		for(int i = 0;i<(*it).second.size();i++)
		{
			// Enter a new state for this submesh-part
			ImmediatePipelineStates.push_back(Engine::GraphicsEngine->CreatePipelineState());

			ImmediatePipelineStates.back()->BaseState.DrawCallType = PipelineState::DCT_DrawIndexed;

			if((*it).first->GetAlphaFunc() > 1)
				ImmediatePipelineStates.back()->BaseState.TranspacenyMode = PipelineState::ETransparencyMode::TM_MASKED;

			Engine::GraphicsEngine->SetupPipelineForStage(STAGE_DRAW_WORLD, ImmediatePipelineStates.back());
				
			ImmediatePipelineStates.back()->BaseState.VertexBuffers[0] = (*it).second[i]->MeshVertexBuffer;
			ImmediatePipelineStates.back()->BaseState.IndexBuffer = (*it).second[i]->MeshIndexBuffer;
			ImmediatePipelineStates.back()->BaseState.NumIndices = (*it).second[i]->Indices.size();
			ImmediatePipelineStates.back()->BaseState.NumVertices = (*it).second[i]->Vertices.size();


			// Huge safety-check to see if gothic didn't mess this up
			if((*it).first &&
				(*it).first->GetTexture() &&
				(*it).first->GetTexture()->GetSurface() &&
				(*it).first->GetTexture()->GetSurface()->GetEngineTexture())
				ImmediatePipelineStates.back()->BaseState.TextureIDs[0] = (*it).first->GetTexture()->GetSurface()->GetEngineTexture()->GetID();

			

			// Enter API-Specific values into the state-object
			Engine::GraphicsEngine->FillPipelineStateObject(ImmediatePipelineStates.back());
		}
	}
}

/** Registers an instance */
void GStaticMeshVisual::RegisterInstance(const RenderInfo& info)
{
	// Add the world-matrix of this vob to the instancing buffer
	unsigned int arrayPos = Instancing.NumRegisteredInstances * sizeof(VobInstanceInfo);

	// If the buffer is too small, resize it
	if(arrayPos + sizeof(VobInstanceInfo) > Instancing.InstancingBuffer->GetSizeInBytes())
	{
		IncreaseInstancingBufferSize();
	}

	// Check if this is the first instance we need to register here
	if(Instancing.NumRegisteredInstances == 0)
	{
		// Since we have at least one instance to fill, map the buffer now and save the datapointer
		UINT size;
		XLE(Instancing.InstancingBuffer->Map(BaseVertexBuffer::M_WRITE_DISCARD, (void**)&Instancing.InstancingBufferData, &size));

		int s = 0;
		for(auto it = VisualInfo.Meshes.begin();it != VisualInfo.Meshes.end(); it++)
		{
			std::vector<MeshInfo*>& meshes = (*it).second;
			for(int i=0;i<meshes.size();i++)
			{

				// Huge safety-check to see if gothic didn't mess this up
				if(PipelineStates[s]->BaseState.TextureIDs[0] == 0xFFFF)
				{

					// Only draw if the texture is loaded
					if((*it).first->GetTexture() && (*it).first->GetTexture()->CacheIn(0.6f) != zRES_CACHED_IN)
					{
						//s++;
						//continue;
						PipelineStates[s]->BaseState.TextureIDs[0] = 0;
					}

					// Get texture ID if everything is allright
					if((*it).first &&
						(*it).first->GetTexture() &&
						(*it).first->GetTexture()->GetSurface() &&
						(*it).first->GetTexture()->GetSurface()->GetEngineTexture())
						PipelineStates[s]->BaseState.TextureIDs[0] = (*it).first->GetTexture()->GetSurface()->GetEngineTexture()->GetID();
				}

				// Give our instancingbuffer to the state if it changed
				if(PipelineStates[s]->BaseState.VertexBuffers[1] != Engine::Game->GetWorld()->GetVobInstanceRemapBuffer()
					|| PipelineStates[s]->BaseState.StructuredBuffersVS[0] != Engine::Game->GetWorld()->GetVobInstanceBuffer())
				{
					PipelineStates[s]->BaseState.VertexBuffers[1] = Engine::Game->GetWorld()->GetVobInstanceRemapBuffer();
					PipelineStates[s]->BaseState.VertexStride[1] = sizeof(VobInstanceRemapInfo);

					PipelineStates[s]->BaseState.StructuredBuffersVS[0] = Engine::Game->GetWorld()->GetVobInstanceBuffer();

					// Reinit the state
					Engine::GraphicsEngine->FillPipelineStateObject(PipelineStates[s]);
				
					PipelineStates[s]->BaseState.NumInstances = 0;
				}

				// Register this state, instances are added afterwards
				//Engine::GraphicsEngine->PushPipelineState(PipelineStates[s]);
				s++;
			}
		}
	}

	if(!Instancing.InstancingBufferData)
		return; // Failed to map?

	// arrayPos is valid, add the instance
	//memcpy((Instancing.InstancingBufferData) + arrayPos, &info.CallingVob->GetInstanceInfo(), sizeof(VobInstanceInfo));
	
	//VobInstanceInfo* ii = ((VobInstanceInfo*)(Instancing.InstancingBufferData + arrayPos));
	//*ii = info.CallingVob->GetInstanceInfo();
	//std::copy(&info.CallingVob->GetInstanceInfo(), (&info.CallingVob->GetInstanceInfo()) + 1, ii);

	Instancing.NumRegisteredInstances++;

	// Save, in case we have to recreate the buffer. // TODO: This is only a temporary solution for the BSP-Pre-Draw
	Instancing.FrameInstanceData.push_back(info.CallingVob->GetInstanceInfo());

	/*int s = 0;
	for(auto it = VisualInfo.Meshes.begin();it != VisualInfo.Meshes.end(); it++)
	{
		std::vector<MeshInfo*>& meshes = (*it).second;
		for(int i=0;i<meshes.size();i++)
		{
			PipelineStates[s]->BaseState.NumInstances = Instancing.NumRegisteredInstances;
			s++;
		}
	}*/
}

/** Just adds a static instance */
int* GStaticMeshVisual::AddStaticInstance(const VobInstanceRemapInfo& remapInfo)
{
	if(remapInfo.InstanceRemapIndex != -1)
	{
		GVisual::AddStaticInstance(remapInfo);

		Instancing.InstanceTypeIndex = Engine::Game->GetWorld()->RegisterVobInstance(Instancing.InstanceTypeIndex, remapInfo, &Instancing.InstanceBufferOffset);
		Instancing.NumRegisteredInstances++;
	}

	return &Instancing.InstanceTypeIndex;
}

/** Draws the visual for the given vob */
void GStaticMeshVisual::DrawVisual(const RenderInfo& info)
{
	GVisual::DrawVisual(info);

	//RegisterInstance(info);
	/*Instancing.InstanceTypeIndex = Engine::Game->GetWorld()->RegisterVobInstance(Instancing.InstanceTypeIndex, info.CallingVob->GetInstanceRemapInfo(), &Instancing.InstanceBufferOffset);
	Instancing.NumRegisteredInstances++;

	return;*/

	// Make sure the vob has enought slots for pipeline states
	if(ImmediatePipelineStates.size() < ImmediatePipelineStates.size())
		ImmediatePipelineStates.resize(PipelineStates.size());

	// Set up states and draw mesh
	int s=0;
	for(auto it = VisualInfo.Meshes.begin();it != VisualInfo.Meshes.end(); it++)
	{
		std::vector<MeshInfo*>& meshes = (*it).second;
		for(int i=0;i<meshes.size();i++)
		{
#ifndef PUBLIC_RELEASE
			if(s >= ImmediatePipelineStates.size())
			{
				LogError() << "GStaticMeshVisual needs more pipeline-states than available!";
				break;
			}
#endif

			// Huge safety-check to see if gothic didn't mess this up
			if(ImmediatePipelineStates[s]->BaseState.TextureIDs[0] == 0xFFFF ||
				ImmediatePipelineStates[s]->BaseState.ConstantBuffersVS[1] == NULL)
			{

				// Only draw if the texture is loaded
				if((*it).first->GetTexture() && (*it).first->GetTexture()->CacheIn(0.6f) != zRES_CACHED_IN)
				{
					s++;
					continue;
				}

				// Get texture ID if everything is allright
				if((*it).first &&
					(*it).first->GetTexture() &&
					(*it).first->GetTexture()->GetSurface() &&
					(*it).first->GetTexture()->GetSurface()->GetEngineTexture())
					ImmediatePipelineStates[s]->BaseState.TextureIDs[0] = (*it).first->GetTexture()->GetSurface()->GetEngineTexture()->GetID();
			}

			ImmediatePipelineStates[s]->BaseState.ConstantBuffersVS[1] = info.InstanceCB;
			Engine::GraphicsEngine->FillPipelineStateObject(ImmediatePipelineStates[s]);

			// Put distance into the state
			//info.CallingVob->GetPipelineStates()[s]->SortItem.state.Depth = info.Distance > 0 ? std::min((UINT)info.Distance, (UINT)0xFFFFFF) : 0xFFFFFF;
			//info.CallingVob->GetPipelineStates()[s]->SortItem.state.Build(info.CallingVob->GetPipelineStates()[s]->BaseState);

			PipelineState* transientState = Engine::GraphicsEngine->CreatePipelineState(ImmediatePipelineStates[s]);
			transientState->TransientState = true;

			// Push state to render-queue
			Engine::GraphicsEngine->PushPipelineState(transientState);

			//Engine::GraphicsEngine->BindPipelineState(PipelineStates[s]);
			//Engine::GraphicsEngine->DrawPipelineState(PipelineStates[s]);
			//Engine::GraphicsEngine->GetLineRenderer()->AddWireframeMesh(meshes[i]->Vertices, meshes[i]->Indices, D3DXVECTOR4(0,1,0,1), &world);

			s++;
		}
	}
}


/** Called on a new frame */
void GStaticMeshVisual::OnBeginDraw()
{
	Instancing.NumRegisteredInstances = 0;
	Instancing.InstanceTypeIndex = -1;
	Instancing.FrameInstanceData.clear();
}

/** Called when we are done drawing */
void GStaticMeshVisual::OnEndDraw()
{
	GVisual::OnEndDraw();

	EndDrawInstanced();
	/*// Check if we actually had something to do this frame
	if(Instancing.NumRegisteredInstances != 0)
	{
		// Yes! This means that the instancing-buffer currently is mapped.
		// Unmap it now.
		XLE(Instancing.InstancingBuffer->Unmap());
		Instancing.InstancingBufferData = NULL;

		Instancing.NumRegisteredInstances = 0;
		Instancing.FrameInstanceData.clear();
	}*/
}

/** Draws the instances registered in this buffer */
void GStaticMeshVisual::DrawInstances()
{

}

/** Doubles the size the instancing buffer can hold (Recreates buffer) */
XRESULT GStaticMeshVisual::IncreaseInstancingBufferSize()
{
	std::vector<VobInstanceInfo> tempData;

	// Unmap, in case we are mapped
	if(Instancing.InstancingBufferData)
	{
		// Save old data
		//tempData.resize(Instancing.NumRegisteredInstances);
		//memcpy(&tempData[0], Instancing.InstancingBufferData, Instancing.NumRegisteredInstances * sizeof(VobInstanceInfo));

		XLE(Instancing.InstancingBuffer->Unmap());
		Instancing.InstancingBufferData = NULL;
	}

	// Increase size
	unsigned int size = Instancing.InstancingBuffer->GetSizeInBytes();

	if(Instancing.NumRegisteredInstances != 0)
	{
		// This is more accurate in case we have this filled
		size = Instancing.NumRegisteredInstances * sizeof(VobInstanceInfo);
	}

	// Recreate the buffer
	delete Instancing.InstancingBuffer;
	Engine::GraphicsEngine->CreateVertexBuffer(&Instancing.InstancingBuffer);

	// Create a new buffer with the size doubled
	XLE(Instancing.InstancingBuffer->Init(NULL, size * 2, BaseVertexBuffer::B_VERTEXBUFFER, BaseVertexBuffer::U_DYNAMIC, BaseVertexBuffer::CA_WRITE));

	// Fill old data
	// Instancing.InstancingBuffer->UpdateBuffer(&tempData[0], tempData.size() * sizeof(VobInstanceInfo));

	// FIXME: This might leads to some objects disappearing for one frame because the buffer got emptied, but I don't
	// want to write a full second array of instance-infos just because of that.
	//Instancing.NumRegisteredInstances = 0;

	return XR_SUCCESS;
}

/** Draws a batch of instance-infos. Returns a pointer to the instance-buffer and it's size.
If the buffer is too small use .*/
void GStaticMeshVisual::BeginDrawInstanced()
{
	Instancing.InstanceTypeIndex = -1;

	// Already mapped?
	/*if(Instancing.InstancingBufferData)
	{
		return;
	}

	// Map the buffer now and save the datapointer
	UINT size;
	XLE(Instancing.InstancingBuffer->Map(BaseVertexBuffer::M_WRITE_DISCARD, (void**)&Instancing.InstancingBufferData, &size));*/
}

/** Can be called before you add instances to the buffer, so the visual can increase the size of the instancing buffer if needed */
bool GStaticMeshVisual::OnAddInstances(int numInstances, VobInstanceInfo* instances)
{
	Instancing.NumRegisteredInstances += numInstances;

	return true;
}

/** Finishes the instanced-draw-call */
void GStaticMeshVisual::EndDrawInstanced()
{
	if(Instancing.InstanceTypeIndex != -1)
	{
		int s = 0;
		for(auto it = VisualInfo.Meshes.begin();it != VisualInfo.Meshes.end(); it++)
		{
			std::vector<MeshInfo*>& meshes = (*it).second;
			for(int i=0;i<meshes.size();i++)
			{

				// Huge safety-check to see if gothic didn't mess this up
				if(PipelineStates[s]->BaseState.TextureIDs[0] == 0xFFFF)
				{

					// Only draw if the texture is loaded
					if((*it).first->GetTexture() && (*it).first->GetTexture()->CacheIn(0.6f) != zRES_CACHED_IN)
					{
						//s++;
						//continue;
						PipelineStates[s]->BaseState.TextureIDs[0] = 0;
					}

					// Get texture ID if everything is allright
					if((*it).first &&
						(*it).first->GetTexture() &&
						(*it).first->GetTexture()->GetSurface() &&
						(*it).first->GetTexture()->GetSurface()->GetEngineTexture())
					{
						PipelineStates[s]->BaseState.TextureIDs[0] = (*it).first->GetTexture()->GetSurface()->GetEngineTexture()->GetID();

						// Get Alphatest
						if((*it).first->GetAlphaFunc() > 1 || (*it).first->GetTexture()->HasAlphaChannel())
							PipelineStates[s]->BaseState.TranspacenyMode = PipelineState::ETransparencyMode::TM_MASKED;

						Engine::GraphicsEngine->SetupPipelineForStage(STAGE_DRAW_WORLD, PipelineStates[s]);
					}
				}

				// Give our instancingbuffer to the state if it changed
				if(PipelineStates[s]->BaseState.VertexBuffers[1] != Engine::Game->GetWorld()->GetVobInstanceRemapBuffer()
					|| PipelineStates[s]->BaseState.StructuredBuffersVS[0] != Engine::Game->GetWorld()->GetVobInstanceBuffer())
				{
					PipelineStates[s]->BaseState.VertexBuffers[1] = Engine::Game->GetWorld()->GetVobInstanceRemapBuffer();
					PipelineStates[s]->BaseState.VertexStride[1] = sizeof(VobInstanceRemapInfo);

					PipelineStates[s]->BaseState.StructuredBuffersVS[0] = Engine::Game->GetWorld()->GetVobInstanceBuffer();

					// Reinit the state
					Engine::GraphicsEngine->FillPipelineStateObject(PipelineStates[s]);
				}

				// Unbind any transforms-buffer set here so we don't have useless state-changes
				PipelineStates.back()->BaseState.ConstantBuffersVS[0] = NULL;

				// Set instancing info
				PipelineStates[s]->BaseState.NumInstances = Instancing.NumRegisteredInstances;
				PipelineStates[s]->BaseState.InstanceOffset = Instancing.InstanceBufferOffset;

				// Register this state
				Engine::GraphicsEngine->PushPipelineState(PipelineStates[s]);
				s++;
			}
		}
	}

	Instancing.InstanceTypeIndex = -1;

	// Check if we actually had something to do this frame
	/*if(Instancing.InstancingBufferData != NULL)
	{
		// Yes! This means that the instancing-buffer currently is mapped.
		// Unmap it now.

		memcpy(Instancing.InstancingBufferData, &Instancing.FrameInstanceData[0], Instancing.FrameInstanceData.size() * sizeof(VobInstanceInfo));
		//Toolbox::X_aligned_memcpy_sse2(Instancing.InstancingBufferData, &Instancing.FrameInstanceData[0], Instancing.FrameInstanceData.size() * sizeof(VobInstanceInfo));
		//std::copy(Instancing.FrameInstanceData.begin(), Instancing.FrameInstanceData.end(), (VobInstanceInfo*)Instancing.InstancingBufferData);

		XLE(Instancing.InstancingBuffer->Unmap());
		Instancing.InstancingBufferData = NULL;

		Instancing.FrameInstanceData.clear();
	
		if(Instancing.NumRegisteredInstances != 0)
		{
			int s = 0;
			for(auto it = VisualInfo.Meshes.begin();it != VisualInfo.Meshes.end(); it++)
			{
				std::vector<MeshInfo*>& meshes = (*it).second;
				for(int i=0;i<meshes.size();i++)
				{

					// Huge safety-check to see if gothic didn't mess this up
					if(PipelineStates[s]->BaseState.TextureIDs[0] == 0xFFFF)
					{

						// Only draw if the texture is loaded
						if((*it).first->GetTexture() && (*it).first->GetTexture()->CacheIn(0.6f) != zRES_CACHED_IN)
						{
							//s++;
							//continue;
							PipelineStates[s]->BaseState.TextureIDs[0] = 0;
						}

						// Get texture ID if everything is allright
						if((*it).first &&
							(*it).first->GetTexture() &&
							(*it).first->GetTexture()->GetSurface() &&
							(*it).first->GetTexture()->GetSurface()->GetEngineTexture())
							PipelineStates[s]->BaseState.TextureIDs[0] = (*it).first->GetTexture()->GetSurface()->GetEngineTexture()->GetID();
					}

					// Give our instancingbuffer to the state
					//if(PipelineStates[s]->BaseState.VertexBuffers[1] != Instancing.InstancingBuffer)
					{
						PipelineStates[s]->BaseState.VertexBuffers[1] = Instancing.InstancingBuffer;
						PipelineStates[s]->BaseState.VertexStride[1] = sizeof(VobInstanceInfo);
						Engine::GraphicsEngine->FillPipelineStateObject(PipelineStates[s]);
					}
					PipelineStates[s]->BaseState.NumInstances = Instancing.NumRegisteredInstances;

					// Register this state
					Engine::GraphicsEngine->PushPipelineState(PipelineStates[s]);
					s++;
				}
			}
		}
	}

	Instancing.NumRegisteredInstances = 0;*/
}