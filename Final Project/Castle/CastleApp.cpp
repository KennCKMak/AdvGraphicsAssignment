//***************************************************************************************
// CastleApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"
#include "../../Common/Camera.h"
#include <time.h>


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};

class CastleApp : public D3DApp
{
public:
	CastleApp(HINSTANCE hInstance);
	CastleApp(const CastleApp& rhs) = delete;
	CastleApp& operator=(const CastleApp& rhs) = delete;
	~CastleApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayouts();
	void BuildLandGeometry();
	void BuildWavesGeometry();
	void BuildShapeGeometry();
	void BuildTreeSpritesGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();

	void BuildRenderItems();
	void BuildWaves();
	void BuildWalls();
	void BuildTowers();
	void BuildRailings();
	void BuildRailAndSpikes(float posX, float posY, float posZ, int dirX, int dirZ);
	void BuildInner();
	void BuildMaze();

	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mStdInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

	RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

	PassConstants mMainPassCB;

	//My eye position
	XMFLOAT3 mEyePos = { 232.0f, 0.0f, 0.0f };// { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f*XM_PI;
	float mPhi = XM_PIDIV2 - 0.1f;
	float mRadius = 50.0f;

	Camera mCamera;

	POINT mLastMousePos;

	//new counter, incremented for each new primitive obj
	UINT objCBIndex = 0;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		CastleApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

CastleApp::CastleApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

CastleApp::~CastleApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool CastleApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(350.0f, 2.0f, 0.0f);

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
	srand(time(NULL));

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayouts();
	BuildLandGeometry();
	BuildWavesGeometry();
	BuildShapeGeometry();
	BuildTreeSpritesGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void CastleApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void CastleApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

void CastleApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CastleApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void CastleApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void CastleApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta -= dx;
		mPhi -= dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.2f*static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f*static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 450.0f);
	}


	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void CastleApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	//WASD for movement, Space/Shift for vert movement
	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(40.0f*dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-40.0f*dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-40.0f*dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(40.0f*dt);

	if (GetAsyncKeyState(VK_SPACE) & 0x8000)
		mCamera.Rise(40.0f*dt);

	if (GetAsyncKeyState(VK_LSHIFT) & 0x8000)
		mCamera.Lower(40.0f*dt);

	mCamera.UpdateViewMatrix();
}

void CastleApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mCamera.UpdateViewMatrix();
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void CastleApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	//tiling u & v
	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if(tu >= 1.0f)
		tu -= 1.0f;

	if(tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void CastleApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void CastleApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void CastleApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	//Directional light
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	//Point light 1
	mMainPassCB.Lights[1].Strength = { 10.f, 10.0f, 4.0f };
	mMainPassCB.Lights[1].FalloffStart = 0.0f;
	mMainPassCB.Lights[1].FalloffEnd = 25.0f;
	mMainPassCB.Lights[1].Position = { 0.0f, 19.0f, -15.0f };
	//Point light 2
	mMainPassCB.Lights[2].Strength = { 10.f, 10.0f, 4.0f };
	mMainPassCB.Lights[2].FalloffStart = 0.0f;
	mMainPassCB.Lights[2].FalloffEnd = 25.0f;
	mMainPassCB.Lights[2].Position = { 0.0f, 19.0f, 15.0f };
	//Point light 3
	mMainPassCB.Lights[3].Strength = { 10.f, 10.0f, 4.0f };
	mMainPassCB.Lights[3].FalloffStart = 0.0f;
	mMainPassCB.Lights[3].FalloffEnd = 25.0f;
	mMainPassCB.Lights[3].Position = { 30.0f, 19.0f, -15.0f };
	//Point light 4
	mMainPassCB.Lights[4].Strength = { 10.f, 10.0f, 4.0f };
	mMainPassCB.Lights[4].FalloffStart = 0.0f;
	mMainPassCB.Lights[4].FalloffEnd = 22.0f;
	mMainPassCB.Lights[4].Position = { -30.0f, 19.0f, -15.0f };
	//Point light 5
	mMainPassCB.Lights[5].Strength = { 10.f, 10.0f, 4.0f };
	mMainPassCB.Lights[5].FalloffStart = 0.0f;
	mMainPassCB.Lights[5].FalloffEnd = 25.0f;
	mMainPassCB.Lights[5].Position = { 30.0f, 19.0f, 15.0f };
	//Point light 6
	mMainPassCB.Lights[6].Strength = { 10.f, 10.0f, 4.0f };
	mMainPassCB.Lights[6].FalloffStart = 0.0f;
	mMainPassCB.Lights[6].FalloffEnd = 22.0f;
	mMainPassCB.Lights[6].Position = { -30.0f, 19.0f, 15.0f };
	//Spot light
	mMainPassCB.Lights[7].Strength = { 10.f, 0.0f, 0.0f };
	mMainPassCB.Lights[7].Position = { -36.0f, 15.0f, 0.0f };
	mMainPassCB.Lights[7].SpotPower = 5.0f;
	mMainPassCB.Lights[7].FalloffStart = 0.0f;
	mMainPassCB.Lights[7].FalloffEnd = 30.0f;


	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void CastleApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.1f, 0.25f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

// Load all of the textures we are going to use into memory.
void CastleApp::LoadTextures()
{
	// Create "grass" texture
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../../Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	// Create "water" texture
	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../../Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	// Create "tile" texture
	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"../../Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	// Create "wood" texture
	auto woodTex = std::make_unique<Texture>();
	woodTex->Name = "woodTex";
	woodTex->Filename = L"../../Textures/wood.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodTex->Filename.c_str(),
		woodTex->Resource, woodTex->UploadHeap));

	// Create "metal" texture
	auto metalTex = std::make_unique<Texture>();
	metalTex->Name = "metalTex";
	metalTex->Filename = L"../../Textures/metal.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), metalTex->Filename.c_str(),
		metalTex->Resource, metalTex->UploadHeap));

	// Create "glass" texture
	auto glassTex = std::make_unique<Texture>();
	glassTex->Name = "glassTex";
	glassTex->Filename = L"../../Textures/glass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), glassTex->Filename.c_str(),
		glassTex->Resource, glassTex->UploadHeap));

	// Create "ice" texture
	auto iceTex = std::make_unique<Texture>();
	iceTex->Name = "iceTex";
	iceTex->Filename = L"../../Textures/ice.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), iceTex->Filename.c_str(),
		iceTex->Resource, iceTex->UploadHeap));

	// Create "stone" texture
	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"../../Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	// Create "brick" texture
	auto brick2Tex = std::make_unique<Texture>();
	brick2Tex->Name = "brick2Tex";
	brick2Tex->Filename = L"../../Textures/bricks2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), brick2Tex->Filename.c_str(),
		brick2Tex->Resource, brick2Tex->UploadHeap));

	// Create "tree" texture
	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"../../Textures/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));

	// Add newly created textures into the mTextures list.
	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[tileTex->Name] = std::move(tileTex);
	mTextures[woodTex->Name] = std::move(woodTex);
	mTextures[metalTex->Name] = std::move(metalTex);
	mTextures[glassTex->Name] = std::move(glassTex);
	mTextures[iceTex->Name] = std::move(iceTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[brick2Tex->Name] = std::move(brick2Tex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
}

void CastleApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CastleApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 10;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// Grab all the textures we want to load and store them into a temp variable
	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;
	auto woodTex = mTextures["woodTex"]->Resource;
	auto metalTex = mTextures["metalTex"]->Resource;
	auto glassTex = mTextures["glassTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto brick2Tex = mTextures["brick2Tex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;

	// One by one, we offset the descriptor by 1 and add create our shader resource for all of the,
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = woodTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(woodTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = metalTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(metalTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = glassTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(glassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = iceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = brick2Tex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(brick2Tex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);
}

void CastleApp::BuildShadersAndInputLayouts()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_0");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_0");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_0");

	mStdInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void CastleApp::BuildLandGeometry()
{
	//Generates vertices for our grid and allows for smooth heights with GetHillsHeight
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(100.0f, 100.0f, 50, 50);

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void CastleApp::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i*n + j;
			indices[k + 1] = i*n + j + 1;
			indices[k + 2] = (i + 1)*n + j;

			indices[k + 3] = (i + 1)*n + j;
			indices[k + 4] = i*n + j + 1;
			indices[k + 5] = (i + 1)*n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void CastleApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	//objects created and scaled according to Unity model Transform values
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(1.0f, 1.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(1.0f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.0f, 1.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f, 1.5f, 3);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(0.0f, 1.0f, 1.0f, 6);
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(1.0f, 0.25f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT pyramidVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT coneVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT diamondVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT torusVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT pyramidIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT coneIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT diamondIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT torusIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry torusSubmesh;
	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
	torusSubmesh.StartIndexLocation = torusIndexOffset;
	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		pyramid.Vertices.size() +
		cone.Vertices.size() +
		diamond.Vertices.size() +
		torus.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}
	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;
	}
	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
		vertices[k].TexC = cone.Vertices[i].TexC;
	}
	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;
	}
	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Normal = torus.Vertices[i].Normal;
		vertices[k].TexC = torus.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	//assigning call strings for our geometry
	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;



	mGeometries[geo->Name] = std::move(geo);
}

void CastleApp::BuildTreeSpritesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 32;
	std::array<TreeSpriteVertex, treeCount> vertices;

	//build an object outside x values -98 > x < 306. midpt = 104, +-202.0f diff
	//outside z values -90.1 > z < 90.1, midpt = 0. +-90.1f diff

	//used to determine if we need to move tree outside of castle
	bool inXRange;
	bool inZRange;

	for (UINT i = 0; i < treeCount; ++i)
	{
		inXRange = false;
		inZRange = false;

		//random number along boundaries
		//makes sures the tree spawns OUTSIDE castle and maze grounds

		//first step is to generate random points.
		float x = MathHelper::RandF(-98.0f - 20.0f, 306.0f + 20.0f);
		float z = MathHelper::RandF(-90.1f - 20.0f, 90.1f + 20.0f);

		//Inside the castle - which axis?
		if (x > -98.0f && x < 306.0f)
			inXRange = true;
		if (z > -90.1f && z < 90.1f)
			inZRange = true;

		//if inside castle boundaries (X or Z axis), then move it to the edge and add arbitrary range (20.0f)
		if (inXRange && !inZRange) {
			if (x < 104.0f)
				x = -98.0f - MathHelper::RandF(0.0f, 20.0f);
			else
				x = 306.0f + MathHelper::RandF(0.0f, 20.0f);
		} else if (!inXRange && inZRange) {
			if (z < 0.0f)
				z = -90.1f - MathHelper::RandF(0.0f, 20.0f);
			else
				z = 90.1f + MathHelper::RandF(0.0f, 20.0f);
		} else if (inXRange && inZRange) {
			//if in both boundaries of x & z...
			//choose which side to push towards
			//num = (-1) or (+1) - negative or positive integer
			int num = MathHelper::RandSign();
			if (num < 0) {
				if (x < 104.0f)
					x = -98.0f - MathHelper::RandF(0.0f, 20.0f);
				else
					x = 306.0f + MathHelper::RandF(0.0f, 20.0f);
			} else {
				if (z < 0.0f)
					z = -90.1f - MathHelper::RandF(0.0f, 20.0f);
				else
					z = 90.1f + MathHelper::RandF(0.0f, 20.0f);
			}
		}


		float y = GetHillsHeight(x, z);

		// Move tree slightly above land height.
		y += 8.0f;

		vertices[i].Pos = XMFLOAT3(x, y, z);
		vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
	}

	std::array<std::uint16_t, 32> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29, 30, 31
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void CastleApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for transparent objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	//
	// PSO for alpha tested objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void CastleApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
	}
}

// Configure and build our textures into materials and prepare to be able to apply them to objects.
// Note: We also need to increment the "MatCBIndex" and "DiffuseSrvHeapIndex" by 1 each time
// we add a new Material, otherwise textures will not be applied to the correct object. 
void CastleApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto tile = std::make_unique<Material>();
	tile->Name = "tile";
	tile->MatCBIndex = 2;
	tile->DiffuseSrvHeapIndex = 2;
	tile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tile->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile->Roughness = 0.25f;


	auto wood = std::make_unique<Material>();
	wood->Name = "wood";
	wood->MatCBIndex = 3;
	wood->DiffuseSrvHeapIndex = 3;
	wood->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wood->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	wood->Roughness = 0.25f;

	auto metal = std::make_unique<Material>();
	metal->Name = "metal";
	metal->MatCBIndex = 4;
	metal->DiffuseSrvHeapIndex = 4;
	metal->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	metal->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	metal->Roughness = 0.25f;

	auto glass = std::make_unique<Material>();
	glass->Name = "glass";
	glass->MatCBIndex = 5;
	glass->DiffuseSrvHeapIndex = 5;
	glass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	glass->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	glass->Roughness = 0.25f;

	auto ice = std::make_unique<Material>();
	ice->Name = "ice";
	ice->MatCBIndex = 6;
	ice->DiffuseSrvHeapIndex = 6;
	ice->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ice->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	ice->Roughness = 0.25f;

	auto stone = std::make_unique<Material>();
	stone->Name = "stone";
	stone->MatCBIndex = 7;
	stone->DiffuseSrvHeapIndex = 7;
	stone->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	stone->Roughness = 0.25f;

	auto brick2 = std::make_unique<Material>();
	brick2->Name = "brick2";
	brick2->MatCBIndex = 8;
	brick2->DiffuseSrvHeapIndex = 8;
	brick2->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	brick2->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	brick2->Roughness = 0.25f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 9;
	treeSprites->DiffuseSrvHeapIndex = 9;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;

	// After we are done configuring the textures, its time to add it to our materials list.
	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["tile"] = std::move(tile);
	mMaterials["wood"] = std::move(wood);
	mMaterials["metal"] = std::move(metal);
	mMaterials["glass"] = std::move(glass);
	mMaterials["ice"] = std::move(ice);
	mMaterials["stone"] = std::move(stone);
	mMaterials["brick2"] = std::move(brick2);
	mMaterials["treeSprites"] = std::move(treeSprites);
}

void CastleApp::BuildRenderItems()
{

	//floor
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(6.0f, 1.0, 3.0f)
		* XMMatrixTranslation(104.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	//Custom functions to make this area cleaner. Generates castle and maze
	BuildWalls();
	BuildTowers();
	BuildRailings();
	BuildInner();

	BuildMaze();

	BuildWaves();

	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = objCBIndex++;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
	mAllRitems.push_back(std::move(treeSpritesRitem));
}

void CastleApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CastleApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

float CastleApp::GetHillsHeight(float x, float z)const
{
	//this function generates the terrain height...
	//atm we want flat ground so we return 0
	return 0;
	//return 0.10f*(z*sinf(0.1f*x) + x*cosf(0.1f*z));

}

XMFLOAT3 CastleApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	//atm we want ground to be 0, so we multiple the values by 0.000f...
	XMFLOAT3 n(
		-0.000f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
		1.0f,
		-0.00f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

void CastleApp::BuildWaves() {
	auto wavesRitem = std::make_unique<RenderItem>();
	//wavesRitem->World = MathHelper::Identity4x4();

	XMStoreFloat4x4(&wavesRitem->World, XMMatrixScaling(10.0f, 1.0f, 10.0f)
		* XMMatrixTranslation(0.0f, -5.0f, 0.0f));
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wavesRitem->ObjCBIndex = objCBIndex++;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();
	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());
	mAllRitems.push_back(std::move(wavesRitem));
}

void CastleApp::BuildWalls() {
	//gates
	auto gateLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gateLeft->World, XMMatrixScaling(1.0f, 14.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90.0f))
		* XMMatrixTranslation(77.0f, 7.0f, -15.65f));
	XMStoreFloat4x4(&gateLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gateLeft->ObjCBIndex = objCBIndex++;
	gateLeft->Mat = mMaterials["wood"].get();
	gateLeft->Geo = mGeometries["shapeGeo"].get();
	gateLeft->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gateLeft->IndexCount = gateLeft->Geo->DrawArgs["box"].IndexCount;
	gateLeft->StartIndexLocation = gateLeft->Geo->DrawArgs["box"].StartIndexLocation;
	gateLeft->BaseVertexLocation = gateLeft->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gateLeft.get());
	mAllRitems.push_back(std::move(gateLeft));

	auto gateRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gateRight->World, XMMatrixScaling(1.0f, 14.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90.0f))
		* XMMatrixTranslation(77.0f, 7.0f, 15.65f));
	XMStoreFloat4x4(&gateRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gateRight->ObjCBIndex = objCBIndex++;
	gateRight->Mat = mMaterials["wood"].get();
	gateRight->Geo = mGeometries["shapeGeo"].get();
	gateRight->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gateRight->IndexCount = gateRight->Geo->DrawArgs["box"].IndexCount;
	gateRight->StartIndexLocation = gateRight->Geo->DrawArgs["box"].StartIndexLocation;
	gateRight->BaseVertexLocation = gateRight->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gateRight.get());
	mAllRitems.push_back(std::move(gateRight));


	//walls, viewed from gate->back perspective
	auto wallLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallLeft->World, XMMatrixScaling(100.0f, 16.0f, 18.0f) * XMMatrixTranslation(0.0f, 8.0f, -59.0f));
	XMStoreFloat4x4(&wallLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallLeft->ObjCBIndex = objCBIndex++;
	wallLeft->Mat = mMaterials["brick2"].get();
	wallLeft->Geo = mGeometries["shapeGeo"].get();
	wallLeft->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallLeft->IndexCount = wallLeft->Geo->DrawArgs["box"].IndexCount;
	wallLeft->StartIndexLocation = wallLeft->Geo->DrawArgs["box"].StartIndexLocation;
	wallLeft->BaseVertexLocation = wallLeft->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallLeft.get());
	mAllRitems.push_back(std::move(wallLeft));

	auto wallRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRight->World, XMMatrixScaling(100.0f, 16.0f, 18.0f)*XMMatrixTranslation(0.0f, 8.0f, 59.0f));
	XMStoreFloat4x4(&wallRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallRight->ObjCBIndex = objCBIndex++;
	wallRight->Mat = mMaterials["brick2"].get();
	wallRight->Geo = mGeometries["shapeGeo"].get();
	wallRight->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRight->IndexCount = wallRight->Geo->DrawArgs["box"].IndexCount;
	wallRight->StartIndexLocation = wallRight->Geo->DrawArgs["box"].StartIndexLocation;
	wallRight->BaseVertexLocation = wallRight->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRight.get());
	mAllRitems.push_back(std::move(wallRight));

	auto wallBack = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallBack->World, XMMatrixScaling(100.0f, 16.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(-59.0f, 8.0f, 0.0f));
	XMStoreFloat4x4(&wallBack->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallBack->ObjCBIndex = objCBIndex++;
	wallBack->Mat = mMaterials["brick2"].get();
	wallBack->Geo = mGeometries["shapeGeo"].get();
	wallBack->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallBack->IndexCount = wallBack->Geo->DrawArgs["box"].IndexCount;
	wallBack->StartIndexLocation = wallBack->Geo->DrawArgs["box"].StartIndexLocation;
	wallBack->BaseVertexLocation = wallBack->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallBack.get());
	mAllRitems.push_back(std::move(wallBack));

	auto wallFrontL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontL->World, XMMatrixScaling(35.0f, 16.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(59.0f, 8.0f, -32.5f));
	XMStoreFloat4x4(&wallFrontL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontL->ObjCBIndex = objCBIndex++;
	wallFrontL->Mat = mMaterials["brick2"].get();
	wallFrontL->Geo = mGeometries["shapeGeo"].get();
	wallFrontL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontL->IndexCount = wallFrontL->Geo->DrawArgs["box"].IndexCount;
	wallFrontL->StartIndexLocation = wallFrontL->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontL->BaseVertexLocation = wallFrontL->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallFrontL.get());
	mAllRitems.push_back(std::move(wallFrontL));

	auto wallFrontR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontR->World, XMMatrixScaling(35.0f, 16.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(59.0f, 8.0f, 32.5f));
	XMStoreFloat4x4(&wallFrontR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontR->ObjCBIndex = objCBIndex++;
	wallFrontR->Mat = mMaterials["brick2"].get();
	wallFrontR->Geo = mGeometries["shapeGeo"].get();
	wallFrontR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontR->IndexCount = wallFrontR->Geo->DrawArgs["box"].IndexCount;
	wallFrontR->StartIndexLocation = wallFrontR->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontR->BaseVertexLocation = wallFrontR->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallFrontR.get());
	mAllRitems.push_back(std::move(wallFrontR));

	auto wallFrontM = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontM->World, XMMatrixScaling(35.0f, 2.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(59.0f, 15.0f, 0.0f));
	XMStoreFloat4x4(&wallFrontM->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontM->ObjCBIndex = objCBIndex++;
	wallFrontM->Mat = mMaterials["brick2"].get();
	wallFrontM->Geo = mGeometries["shapeGeo"].get();
	wallFrontM->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontM->IndexCount = wallFrontM->Geo->DrawArgs["box"].IndexCount;
	wallFrontM->StartIndexLocation = wallFrontM->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontM->BaseVertexLocation = wallFrontM->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallFrontM.get());
	mAllRitems.push_back(std::move(wallFrontM));
}

void CastleApp::BuildTowers() {
	//viewed from front perspective
	auto cylinderFrontL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderFrontL->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(59.0f, 16.5f, -59.0f));
	XMStoreFloat4x4(&cylinderFrontL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderFrontL->ObjCBIndex = objCBIndex++;
	cylinderFrontL->Mat = mMaterials["brick2"].get();
	cylinderFrontL->Geo = mGeometries["shapeGeo"].get();
	cylinderFrontL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderFrontL->IndexCount = cylinderFrontL->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderFrontL->StartIndexLocation = cylinderFrontL->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderFrontL->BaseVertexLocation = cylinderFrontL->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cylinderFrontL.get());
	mAllRitems.push_back(std::move(cylinderFrontL));

	auto coneFrontL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneFrontL->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(59.0f, 52.0f, -59.0f));
	XMStoreFloat4x4(&coneFrontL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneFrontL->ObjCBIndex = objCBIndex++;
	coneFrontL->Mat = mMaterials["wood"].get();
	coneFrontL->Geo = mGeometries["shapeGeo"].get();
	coneFrontL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneFrontL->IndexCount = coneFrontL->Geo->DrawArgs["cone"].IndexCount;
	coneFrontL->StartIndexLocation = coneFrontL->Geo->DrawArgs["cone"].StartIndexLocation;
	coneFrontL->BaseVertexLocation = coneFrontL->Geo->DrawArgs["cone"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(coneFrontL.get());
	mAllRitems.push_back(std::move(coneFrontL));

	auto cylinderFrontR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderFrontR->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(59.0f, 16.5f, 59.0f));
	XMStoreFloat4x4(&cylinderFrontR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderFrontR->ObjCBIndex = objCBIndex++;
	cylinderFrontR->Mat = mMaterials["brick2"].get();
	cylinderFrontR->Geo = mGeometries["shapeGeo"].get();
	cylinderFrontR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderFrontR->IndexCount = cylinderFrontR->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderFrontR->StartIndexLocation = cylinderFrontR->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderFrontR->BaseVertexLocation = cylinderFrontR->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cylinderFrontR.get());
	mAllRitems.push_back(std::move(cylinderFrontR));

	auto coneFrontR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneFrontR->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(59.0f, 52.0f, 59.0f));
	XMStoreFloat4x4(&coneFrontR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneFrontR->ObjCBIndex = objCBIndex++;
	coneFrontR->Mat = mMaterials["wood"].get();
	coneFrontR->Geo = mGeometries["shapeGeo"].get();
	coneFrontR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneFrontR->IndexCount = coneFrontR->Geo->DrawArgs["cone"].IndexCount;
	coneFrontR->StartIndexLocation = coneFrontR->Geo->DrawArgs["cone"].StartIndexLocation;
	coneFrontR->BaseVertexLocation = coneFrontR->Geo->DrawArgs["cone"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(coneFrontR.get());
	mAllRitems.push_back(std::move(coneFrontR));

	auto cylinderBackR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderBackR->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 16.5f, 59.0f));
	XMStoreFloat4x4(&cylinderBackR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderBackR->ObjCBIndex = objCBIndex++;
	cylinderBackR->Mat = mMaterials["brick2"].get();
	cylinderBackR->Geo = mGeometries["shapeGeo"].get();
	cylinderBackR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderBackR->IndexCount = cylinderBackR->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderBackR->StartIndexLocation = cylinderBackR->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderBackR->BaseVertexLocation = cylinderBackR->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cylinderBackR.get());
	mAllRitems.push_back(std::move(cylinderBackR));

	auto coneBackR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneBackR->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 52.0f, 59.0f));
	XMStoreFloat4x4(&coneBackR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneBackR->ObjCBIndex = objCBIndex++;
	coneBackR->Mat = mMaterials["wood"].get();
	coneBackR->Geo = mGeometries["shapeGeo"].get();
	coneBackR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneBackR->IndexCount = coneBackR->Geo->DrawArgs["cone"].IndexCount;
	coneBackR->StartIndexLocation = coneBackR->Geo->DrawArgs["cone"].StartIndexLocation;
	coneBackR->BaseVertexLocation = coneBackR->Geo->DrawArgs["cone"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(coneBackR.get());
	mAllRitems.push_back(std::move(coneBackR));

	auto cylinderBackL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderBackL->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 16.5f, -59.0f));
	XMStoreFloat4x4(&cylinderBackL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderBackL->ObjCBIndex = objCBIndex++;
	cylinderBackL->Mat = mMaterials["brick2"].get();
	cylinderBackL->Geo = mGeometries["shapeGeo"].get();
	cylinderBackL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderBackL->IndexCount = cylinderBackL->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderBackL->StartIndexLocation = cylinderBackL->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderBackL->BaseVertexLocation = cylinderBackL->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(cylinderBackL.get());
	mAllRitems.push_back(std::move(cylinderBackL));

	auto coneBackL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneBackL->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 52.0f, -59.0f));
	XMStoreFloat4x4(&coneBackL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneBackL->ObjCBIndex = objCBIndex++;
	coneBackL->Mat = mMaterials["wood"].get();
	coneBackL->Geo = mGeometries["shapeGeo"].get();
	coneBackL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneBackL->IndexCount = coneBackL->Geo->DrawArgs["cone"].IndexCount;
	coneBackL->StartIndexLocation = coneBackL->Geo->DrawArgs["cone"].StartIndexLocation;
	coneBackL->BaseVertexLocation = coneBackL->Geo->DrawArgs["cone"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(coneBackL.get());
	mAllRitems.push_back(std::move(coneBackL));

}

void CastleApp::BuildRailings() {
	//front wall railings

	// wall z = 59, scale = 18, rail z =
	//outer changed from 67.5 to 71.8, diff 4.3
	//inner changed from 50.5 to 46.2, diff 4.3

	BuildRailAndSpikes(71.8f, 17.0f, 0.0f, 0, 1);
	BuildRailAndSpikes(46.2f, 17.0f, 0.0f, 0, 1);


	BuildRailAndSpikes(-71.8f, 17.0f, 0.0f, 0, 1);
	BuildRailAndSpikes(-46.2f, 17.0f, 0.0f, 0, 1);


	BuildRailAndSpikes(0.0f, 17.0f, 71.8f, 1, 0);
	BuildRailAndSpikes(0.0f, 17.0f, 46.2f, 1, 0);

	BuildRailAndSpikes(0.0f, 17.0f, -71.8f, 1, 0);
	BuildRailAndSpikes(0.0f, 17.0f, -46.2f, 1, 0);


}

void CastleApp::BuildRailAndSpikes(float posX, float posY, float posZ, int dirX, int dirZ) {
	//builds the parts that line the wall.
	//posxyz is the midpoint of the rails, located on the wall
	//dirX & dirZ is used to find out where we align it 
	//i.e. if aligning along x axis, dirX = 1, dirZ = 0. multiplied into rotation

	//the railing
	auto railFrontO = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&railFrontO->World, XMMatrixScaling(100.0f, 2.0f, 1.0f)
		* XMMatrixRotationY(XMConvertToRadians(90 * dirZ))
		* XMMatrixTranslation(posX, posY, posZ));
	XMStoreFloat4x4(&railFrontO->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	railFrontO->ObjCBIndex = objCBIndex++;
	railFrontO->Mat = mMaterials["wood"].get();
	railFrontO->Geo = mGeometries["shapeGeo"].get();
	railFrontO->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	railFrontO->IndexCount = railFrontO->Geo->DrawArgs["box"].IndexCount;
	railFrontO->StartIndexLocation = railFrontO->Geo->DrawArgs["box"].StartIndexLocation;
	railFrontO->BaseVertexLocation = railFrontO->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(railFrontO.get());
	mAllRitems.push_back(std::move(railFrontO));

	//loop: @i=0, build middle point. @i = 1, build 1st spike to left and right, and so on for a total of 9 spikes
	//  4 3 2 1 0 1 2 3 4  placement
	for (int i = 0; i < 5; i++) {
		if (i == 0) {
			//the middle 'point' of the railing
			auto block = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&block->World, XMMatrixScaling(2.0f, 4.0f, 2.0f)
				* XMMatrixTranslation(posX + i*10.0f*dirX, posY + 1.0f, posZ + i*10.0f*dirZ));
			XMStoreFloat4x4(&block->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			block->ObjCBIndex = objCBIndex++;
			block->Mat = mMaterials["stone"].get();
			block->Geo = mGeometries["shapeGeo"].get();
			block->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			block->IndexCount = block->Geo->DrawArgs["box"].IndexCount;
			block->StartIndexLocation = block->Geo->DrawArgs["box"].StartIndexLocation;
			block->BaseVertexLocation = block->Geo->DrawArgs["box"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(block.get());
			mAllRitems.push_back(std::move(block));

			auto pyramid = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&pyramid->World, XMMatrixScaling(3.0f, 3.0f, 3.0f)
				* XMMatrixTranslation(posX + i*10.0f*dirX, posY + 3.0f, posZ + i*10.0f*dirZ));
			XMStoreFloat4x4(&pyramid->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			pyramid->ObjCBIndex = objCBIndex++;
			pyramid->Mat = mMaterials["stone"].get();
			pyramid->Geo = mGeometries["shapeGeo"].get();
			pyramid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			pyramid->IndexCount = pyramid->Geo->DrawArgs["pyramid"].IndexCount;
			pyramid->StartIndexLocation = pyramid->Geo->DrawArgs["pyramid"].StartIndexLocation;
			pyramid->BaseVertexLocation = pyramid->Geo->DrawArgs["pyramid"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramid.get());
			mAllRitems.push_back(std::move(pyramid));
		}
		else {
			
			auto block = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&block->World, XMMatrixScaling(2.0f, 4.0f, 2.0f)
				* XMMatrixTranslation(posX + i*10.0f*dirX, posY + 1.0f, posZ + i*10.0f*dirZ));
			XMStoreFloat4x4(&block->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			block->ObjCBIndex = objCBIndex++;
			block->Mat = mMaterials["stone"].get();
			block->Geo = mGeometries["shapeGeo"].get();
			block->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			block->IndexCount = block->Geo->DrawArgs["box"].IndexCount;
			block->StartIndexLocation = block->Geo->DrawArgs["box"].StartIndexLocation;
			block->BaseVertexLocation = block->Geo->DrawArgs["box"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(block.get());
			mAllRitems.push_back(std::move(block));

			auto pyramid = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&pyramid->World, XMMatrixScaling(3.0f, 3.0f, 3.0f)
				* XMMatrixTranslation(posX + i*10.0f*dirX, posY + 3.0f, posZ + i*10.0f*dirZ));
			XMStoreFloat4x4(&pyramid->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			pyramid->ObjCBIndex = objCBIndex++;
			pyramid->Mat = mMaterials["stone"].get();
			pyramid->Geo = mGeometries["shapeGeo"].get();
			pyramid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			pyramid->IndexCount = pyramid->Geo->DrawArgs["pyramid"].IndexCount;
			pyramid->StartIndexLocation = pyramid->Geo->DrawArgs["pyramid"].StartIndexLocation;
			pyramid->BaseVertexLocation = pyramid->Geo->DrawArgs["pyramid"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramid.get());
			mAllRitems.push_back(std::move(pyramid));

			auto block2 = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&block2->World, XMMatrixScaling(2.0f, 4.0f, 2.0f)
				* XMMatrixTranslation(posX - i*10.0f*dirX, posY + 1.0f, posZ - i*10.0f*dirZ));
			XMStoreFloat4x4(&block2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			block2->ObjCBIndex = objCBIndex++;
			block2->Mat = mMaterials["stone"].get();
			block2->Geo = mGeometries["shapeGeo"].get();
			block2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			block2->IndexCount = block2->Geo->DrawArgs["box"].IndexCount;
			block2->StartIndexLocation = block2->Geo->DrawArgs["box"].StartIndexLocation;
			block2->BaseVertexLocation = block2->Geo->DrawArgs["box"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(block2.get());
			mAllRitems.push_back(std::move(block2));

			auto pyramid2 = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&pyramid2->World, XMMatrixScaling(3.0f, 3.0f, 3.0f)
				* XMMatrixTranslation(posX - i*10.0f*dirX, posY + 3.0f, posZ - i*10.0f*dirZ));
			XMStoreFloat4x4(&pyramid2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			pyramid2->ObjCBIndex = objCBIndex++;
			pyramid2->Mat = mMaterials["stone"].get();
			pyramid2->Geo = mGeometries["shapeGeo"].get();
			pyramid2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			pyramid2->IndexCount = pyramid2->Geo->DrawArgs["pyramid"].IndexCount;
			pyramid2->StartIndexLocation = pyramid2->Geo->DrawArgs["pyramid"].StartIndexLocation;
			pyramid2->BaseVertexLocation = pyramid2->Geo->DrawArgs["pyramid"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramid2.get());
			mAllRitems.push_back(std::move(pyramid2));
		}
	}
}

void CastleApp::BuildInner() {

	//path
	auto floor = std::make_unique<RenderItem>();
	floor->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&floor->World, XMMatrixScaling(230.0f, 1.0, 30.0f)
		* XMMatrixTranslation(60.0f, 0.1f, 0.0f));
	XMStoreFloat4x4(&floor->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	floor->ObjCBIndex = objCBIndex++;
	floor->Mat = mMaterials["tile"].get();
	floor->Geo = mGeometries["shapeGeo"].get();
	floor->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	floor->IndexCount = floor->Geo->DrawArgs["grid"].IndexCount;
	floor->StartIndexLocation = floor->Geo->DrawArgs["grid"].StartIndexLocation;
	floor->BaseVertexLocation = floor->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(floor.get());
	mAllRitems.push_back(std::move(floor));

	//pillars
	//xyz = 0, 0, -15, dX of 30. 
	//x values of -30, 0, 30. z values = -15 or 15
	for (int i = 0; i < 3; i++) {
		auto cylinder = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&cylinder->World, XMMatrixScaling(1.0f, 15.0f, 1.0f)
			* XMMatrixTranslation(-30.0f + 30.0f*i, 7.5f, -15.0f));
		XMStoreFloat4x4(&cylinder->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		cylinder->ObjCBIndex = objCBIndex++;
		cylinder->Mat = mMaterials["metal"].get();
		cylinder->Geo = mGeometries["shapeGeo"].get();
		cylinder->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		cylinder->IndexCount = cylinder->Geo->DrawArgs["cylinder"].IndexCount;
		cylinder->StartIndexLocation = cylinder->Geo->DrawArgs["cylinder"].StartIndexLocation;
		cylinder->BaseVertexLocation = cylinder->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(cylinder.get());
		mAllRitems.push_back(std::move(cylinder));

		auto sphere = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&sphere->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)
			* XMMatrixTranslation(-30.0f + 30.0f*i, 16.5f, -15.0f));
		XMStoreFloat4x4(&sphere->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		sphere->ObjCBIndex = objCBIndex++;
		sphere->Mat = mMaterials["glass"].get();
		sphere->Geo = mGeometries["shapeGeo"].get();
		sphere->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphere->IndexCount = sphere->Geo->DrawArgs["sphere"].IndexCount;
		sphere->StartIndexLocation = sphere->Geo->DrawArgs["sphere"].StartIndexLocation;
		sphere->BaseVertexLocation = sphere->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(sphere.get());
		mAllRitems.push_back(std::move(sphere));


		cylinder = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&cylinder->World, XMMatrixScaling(1.0f, 15.0f, 1.0f)
			* XMMatrixTranslation(-30.0f + 30.0f*i, 7.5f, 15.0f));
		XMStoreFloat4x4(&cylinder->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		cylinder->ObjCBIndex = objCBIndex++;
		cylinder->Mat = mMaterials["metal"].get();
		cylinder->Geo = mGeometries["shapeGeo"].get();
		cylinder->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		cylinder->IndexCount = cylinder->Geo->DrawArgs["cylinder"].IndexCount;
		cylinder->StartIndexLocation = cylinder->Geo->DrawArgs["cylinder"].StartIndexLocation;
		cylinder->BaseVertexLocation = cylinder->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(cylinder.get());
		mAllRitems.push_back(std::move(cylinder));

		sphere = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&sphere->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)
			* XMMatrixTranslation(-30.0f + 30.0f*i, 16.5f, 15.0f));
		XMStoreFloat4x4(&sphere->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		sphere->ObjCBIndex = objCBIndex++;
		sphere->Mat = mMaterials["glass"].get();
		sphere->Geo = mGeometries["shapeGeo"].get();
		sphere->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphere->IndexCount = sphere->Geo->DrawArgs["sphere"].IndexCount;
		sphere->StartIndexLocation = sphere->Geo->DrawArgs["sphere"].StartIndexLocation;
		sphere->BaseVertexLocation = sphere->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(sphere.get());
		mAllRitems.push_back(std::move(sphere));
	}

	//altar

	auto altarLower = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&altarLower->World, XMMatrixScaling(15.0f, 1.0f, 15.0f) * XMMatrixTranslation(-35.0f, 0.6f, 0.0f));
	XMStoreFloat4x4(&altarLower->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	altarLower->ObjCBIndex = objCBIndex++;
	altarLower->Mat = mMaterials["stone"].get();
	altarLower->Geo = mGeometries["shapeGeo"].get();
	altarLower->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	altarLower->IndexCount = altarLower->Geo->DrawArgs["box"].IndexCount;
	altarLower->StartIndexLocation = altarLower->Geo->DrawArgs["box"].StartIndexLocation;
	altarLower->BaseVertexLocation = altarLower->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(altarLower.get());
	mAllRitems.push_back(std::move(altarLower));


	auto altarUpper = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&altarUpper->World, XMMatrixScaling(11.0f, 1.0f, 11.0f) * XMMatrixTranslation(-35.0f, 1.6f, 0.0f));
	XMStoreFloat4x4(&altarUpper->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	altarUpper->ObjCBIndex = objCBIndex++;
	altarUpper->Mat = mMaterials["stone"].get();
	altarUpper->Geo = mGeometries["shapeGeo"].get();
	altarUpper->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	altarUpper->IndexCount = altarUpper->Geo->DrawArgs["box"].IndexCount;
	altarUpper->StartIndexLocation = altarUpper->Geo->DrawArgs["box"].StartIndexLocation;
	altarUpper->BaseVertexLocation = altarUpper->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(altarUpper.get());
	mAllRitems.push_back(std::move(altarUpper));

	//the 'goal'
	auto torus = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&torus->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-35.0f, 3.8f, 0.0f));
	XMStoreFloat4x4(&torus->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	torus->ObjCBIndex = objCBIndex++;
	torus->Mat = mMaterials["ice"].get();
	torus->Geo = mGeometries["shapeGeo"].get();
	torus->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	torus->IndexCount = torus->Geo->DrawArgs["torus"].IndexCount;
	torus->StartIndexLocation = torus->Geo->DrawArgs["torus"].StartIndexLocation;
	torus->BaseVertexLocation = torus->Geo->DrawArgs["torus"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(torus.get());
	mAllRitems.push_back(std::move(torus));
}

void CastleApp::BuildMaze() {

	//floor
	auto floor = std::make_unique<RenderItem>();
	floor->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&floor->World, XMMatrixScaling(115.0f, 1.0f, 138.0f)
		* XMMatrixTranslation(232.0f, 0.1f, 0.0f));
	XMStoreFloat4x4(&floor->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	floor->ObjCBIndex = objCBIndex++;
	floor->Mat = mMaterials["tile"].get();
	floor->Geo = mGeometries["shapeGeo"].get();
	floor->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	floor->IndexCount = floor->Geo->DrawArgs["grid"].IndexCount;
	floor->StartIndexLocation = floor->Geo->DrawArgs["grid"].StartIndexLocation;
	floor->BaseVertexLocation = floor->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(floor.get());
	mAllRitems.push_back(std::move(floor));



	//outer maze walls
	auto wallLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallLeft->World, XMMatrixScaling(1.5f, 25.0f, 78.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(232.0f, 12.5f, -69.2f));
	XMStoreFloat4x4(&wallLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallLeft->ObjCBIndex = objCBIndex++;
	wallLeft->Mat = mMaterials["brick2"].get();
	wallLeft->Geo = mGeometries["shapeGeo"].get();
	wallLeft->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallLeft->IndexCount = wallLeft->Geo->DrawArgs["box"].IndexCount;
	wallLeft->StartIndexLocation = wallLeft->Geo->DrawArgs["box"].StartIndexLocation;
	wallLeft->BaseVertexLocation = wallLeft->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallLeft.get());
	mAllRitems.push_back(std::move(wallLeft));

	auto wallRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRight->World, XMMatrixScaling(1.5f, 25.0f, 78.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(232.0f, 12.5f, 69.2f));
	XMStoreFloat4x4(&wallRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallRight->ObjCBIndex = objCBIndex++;
	wallRight->Mat = mMaterials["brick2"].get();
	wallRight->Geo = mGeometries["shapeGeo"].get();
	wallRight->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRight->IndexCount = wallRight->Geo->DrawArgs["box"].IndexCount;
	wallRight->StartIndexLocation = wallRight->Geo->DrawArgs["box"].StartIndexLocation;
	wallRight->BaseVertexLocation = wallRight->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRight.get());
	mAllRitems.push_back(std::move(wallRight));

	//ratio  54 (unity) -> 37 (code) = 0.68518
	auto wallBackL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallBackL->World, XMMatrixScaling(1.5f, 25.0f, 37.0f)
		* XMMatrixTranslation(174.75f, 12.5f, -42.0f));
	XMStoreFloat4x4(&wallBackL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallBackL->ObjCBIndex = objCBIndex++;
	wallBackL->Mat = mMaterials["brick2"].get();
	wallBackL->Geo = mGeometries["shapeGeo"].get();
	wallBackL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallBackL->IndexCount = wallBackL->Geo->DrawArgs["box"].IndexCount;
	wallBackL->StartIndexLocation = wallBackL->Geo->DrawArgs["box"].StartIndexLocation;
	wallBackL->BaseVertexLocation = wallBackL->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallBackL.get());
	mAllRitems.push_back(std::move(wallBackL));

	auto wallBackR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallBackR->World, XMMatrixScaling(1.5f, 25.0f, 37.0f)
		* XMMatrixTranslation(174.75f, 12.5f, 42.0f));
	XMStoreFloat4x4(&wallBackR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallBackR->ObjCBIndex = objCBIndex++;
	wallBackR->Mat = mMaterials["brick2"].get();
	wallBackR->Geo = mGeometries["shapeGeo"].get();
	wallBackR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallBackR->IndexCount = wallBackR->Geo->DrawArgs["box"].IndexCount;
	wallBackR->StartIndexLocation = wallBackR->Geo->DrawArgs["box"].StartIndexLocation;
	wallBackR->BaseVertexLocation = wallBackR->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallBackR.get());
	mAllRitems.push_back(std::move(wallBackR));

	auto wallFrontL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontL->World, XMMatrixScaling(1.5f, 25.0f, 37.0f)
		* XMMatrixTranslation(289.5f, 12.5f, -42.0f));
	XMStoreFloat4x4(&wallFrontL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontL->ObjCBIndex = objCBIndex++;
	wallFrontL->Mat = mMaterials["brick2"].get();
	wallFrontL->Geo = mGeometries["shapeGeo"].get();
	wallFrontL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontL->IndexCount = wallFrontL->Geo->DrawArgs["box"].IndexCount;
	wallFrontL->StartIndexLocation = wallFrontL->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontL->BaseVertexLocation = wallFrontL->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallFrontL.get());
	mAllRitems.push_back(std::move(wallFrontL));

	auto wallFrontR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontR->World, XMMatrixScaling(1.5f, 25.0f, 37.0f)
		* XMMatrixTranslation(289.5f, 12.5f, 42.0f));
	XMStoreFloat4x4(&wallFrontR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontR->ObjCBIndex = objCBIndex++;
	wallFrontR->Mat = mMaterials["brick2"].get();
	wallFrontR->Geo = mGeometries["shapeGeo"].get();
	wallFrontR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontR->IndexCount = wallFrontR->Geo->DrawArgs["box"].IndexCount;
	wallFrontR->StartIndexLocation = wallFrontR->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontR->BaseVertexLocation = wallFrontR->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallFrontR.get());
	mAllRitems.push_back(std::move(wallFrontR));


	//inner maze, left/right orientation
	//scale value in Unity / 116 * 78
	auto wallL1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallL1->World, XMMatrixScaling(1.5f, 25.0f, 30.95f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(197.87f, 12.5f, -15.77f));
	XMStoreFloat4x4(&wallL1->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallL1->ObjCBIndex = objCBIndex++;
	wallL1->Mat = mMaterials["brick2"].get();
	wallL1->Geo = mGeometries["shapeGeo"].get();
	wallL1->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallL1->IndexCount = wallL1->Geo->DrawArgs["box"].IndexCount;
	wallL1->StartIndexLocation = wallL1->Geo->DrawArgs["box"].StartIndexLocation;
	wallL1->BaseVertexLocation = wallL1->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallL1.get());
	mAllRitems.push_back(std::move(wallL1));

	auto wallL2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallL2->World, XMMatrixScaling(1.5f, 25.0f, 17.48f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(207.9f, 12.5f, -43.46f));
	XMStoreFloat4x4(&wallL2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallL2->ObjCBIndex = objCBIndex++;
	wallL2->Mat = mMaterials["brick2"].get();
	wallL2->Geo = mGeometries["shapeGeo"].get();
	wallL2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallL2->IndexCount = wallL2->Geo->DrawArgs["box"].IndexCount;
	wallL2->StartIndexLocation = wallL2->Geo->DrawArgs["box"].StartIndexLocation;
	wallL2->BaseVertexLocation = wallL2->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallL2.get());
	mAllRitems.push_back(std::move(wallL2));

	auto wallL3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallL3->World, XMMatrixScaling(1.5f, 25.0f, 21.52f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(255.25f, 12.5f, -41.98f));
	XMStoreFloat4x4(&wallL3->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallL3->ObjCBIndex = objCBIndex++;
	wallL3->Mat = mMaterials["brick2"].get();
	wallL3->Geo = mGeometries["shapeGeo"].get();
	wallL3->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallL3->IndexCount = wallL3->Geo->DrawArgs["box"].IndexCount;
	wallL3->StartIndexLocation = wallL3->Geo->DrawArgs["box"].StartIndexLocation;
	wallL3->BaseVertexLocation = wallL3->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallL3.get());
	mAllRitems.push_back(std::move(wallL3));

	auto wallL4 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallL4->World, XMMatrixScaling(1.5f, 25.0f, 33.62f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(264.2f, 12.5f, 15.92f));
	XMStoreFloat4x4(&wallL4->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallL4->ObjCBIndex = objCBIndex++;
	wallL4->Mat = mMaterials["brick2"].get();
	wallL4->Geo = mGeometries["shapeGeo"].get();
	wallL4->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallL4->IndexCount = wallL4->Geo->DrawArgs["box"].IndexCount;
	wallL4->StartIndexLocation = wallL4->Geo->DrawArgs["box"].StartIndexLocation;
	wallL4->BaseVertexLocation = wallL4->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallL4.get());
	mAllRitems.push_back(std::move(wallL4));

	auto wallL5 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallL5->World, XMMatrixScaling(1.5f, 25.0f, 47.07f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(235.79f, 12.5f, 37.07f));
	XMStoreFloat4x4(&wallL5->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallL5->ObjCBIndex = objCBIndex++;
	wallL5->Mat = mMaterials["brick2"].get();
	wallL5->Geo = mGeometries["shapeGeo"].get();
	wallL5->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallL5->IndexCount = wallL5->Geo->DrawArgs["box"].IndexCount;
	wallL5->StartIndexLocation = wallL5->Geo->DrawArgs["box"].StartIndexLocation;
	wallL5->BaseVertexLocation = wallL5->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallL5.get());
	mAllRitems.push_back(std::move(wallL5));

	//walls perpendicular to front view
	auto wallF1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallF1->World, XMMatrixScaling(1.5f, 25.0f, 21.52f)
		* XMMatrixTranslation(270.5f, 12.5f, -26.68f));
	XMStoreFloat4x4(&wallF1->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallF1->ObjCBIndex = objCBIndex++;
	wallF1->Mat = mMaterials["brick2"].get();
	wallF1->Geo = mGeometries["shapeGeo"].get();
	wallF1->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallF1->IndexCount = wallF1->Geo->DrawArgs["box"].IndexCount;
	wallF1->StartIndexLocation = wallF1->Geo->DrawArgs["box"].StartIndexLocation;
	wallF1->BaseVertexLocation = wallF1->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallF1.get());
	mAllRitems.push_back(std::move(wallF1));

	auto wallF2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallF2->World, XMMatrixScaling(1.5f, 25.0f, 39.01f)
		* XMMatrixTranslation(240.0f, 12.5f, -12.98f));
	XMStoreFloat4x4(&wallF2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallF2->ObjCBIndex = objCBIndex++;
	wallF2->Mat = mMaterials["brick2"].get();
	wallF2->Geo = mGeometries["shapeGeo"].get();
	wallF2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallF2->IndexCount = wallF2->Geo->DrawArgs["box"].IndexCount;
	wallF2->StartIndexLocation = wallF2->Geo->DrawArgs["box"].StartIndexLocation;
	wallF2->BaseVertexLocation = wallF2->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallF2.get());
	mAllRitems.push_back(std::move(wallF2));

	auto wallF3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallF3->World, XMMatrixScaling(1.5f, 25.0f, 22.19f)
		* XMMatrixTranslation(220.15f, 12.5f, 0.4f));
	XMStoreFloat4x4(&wallF3->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallF3->ObjCBIndex = objCBIndex++;
	wallF3->Mat = mMaterials["brick2"].get();
	wallF3->Geo = mGeometries["shapeGeo"].get();
	wallF3->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallF3->IndexCount = wallF3->Geo->DrawArgs["box"].IndexCount;
	wallF3->StartIndexLocation = wallF3->Geo->DrawArgs["box"].StartIndexLocation;
	wallF3->BaseVertexLocation = wallF3->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallF3.get());
	mAllRitems.push_back(std::move(wallF3));

	auto wallF4 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallF4->World, XMMatrixScaling(1.5f, 25.0f, 36.31f)
		* XMMatrixTranslation(201.0f, 12.5f, 10.87f));
	XMStoreFloat4x4(&wallF4->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallF4->ObjCBIndex = objCBIndex++;
	wallF4->Mat = mMaterials["brick2"].get();
	wallF4->Geo = mGeometries["shapeGeo"].get();
	wallF4->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallF4->IndexCount = wallF4->Geo->DrawArgs["box"].IndexCount;
	wallF4->StartIndexLocation = wallF4->Geo->DrawArgs["box"].StartIndexLocation;
	wallF4->BaseVertexLocation = wallF4->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallF4.get());
	mAllRitems.push_back(std::move(wallF4));

	auto wallF5 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallF5->World, XMMatrixScaling(1.5f, 25.0f, 19.5f)
		* XMMatrixTranslation(195.66, 12.5f, -29.55));
	XMStoreFloat4x4(&wallF5->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallF5->ObjCBIndex = objCBIndex++;
	wallF5->Mat = mMaterials["brick2"].get();
	wallF5->Geo = mGeometries["shapeGeo"].get();
	wallF5->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallF5->IndexCount = wallF5->Geo->DrawArgs["box"].IndexCount;
	wallF5->StartIndexLocation = wallF5->Geo->DrawArgs["box"].StartIndexLocation;
	wallF5->BaseVertexLocation = wallF5->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(wallF5.get());
	mAllRitems.push_back(std::move(wallF5));


}