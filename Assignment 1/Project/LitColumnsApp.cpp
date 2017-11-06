//***************************************************************************************
// LitColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

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

class LitColumnsApp : public D3DApp
{
public:
    LitColumnsApp(HINSTANCE hInstance);
    LitColumnsApp(const LitColumnsApp& rhs) = delete;
    LitColumnsApp& operator=(const LitColumnsApp& rhs) = delete;
    ~LitColumnsApp();

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

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);


	void BuildWalls();
	void BuildTowers();
	void BuildRailings();
	void BuildRailAndSpikes(float posX, float posY, float posZ, int dirX, int dirZ);
	void BuildInner();
 
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

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = 0.2f*XM_PI;
    float mRadius = 15.0f;

    POINT mLastMousePos;

	UINT objectIndex = 0;

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
        LitColumnsApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

LitColumnsApp::LitColumnsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

LitColumnsApp::~LitColumnsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool LitColumnsApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
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
 
void LitColumnsApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void LitColumnsApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
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
}

void LitColumnsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

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

void LitColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void LitColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void LitColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void LitColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
}
 
void LitColumnsApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x*3, mEyePos.y*3, mEyePos.z*3, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void LitColumnsApp::AnimateMaterials(const GameTimer& gt)
{
	
}

void LitColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
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

void LitColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
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

void LitColumnsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

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
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.1f, 0.1f, 0.2f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void LitColumnsApp::BuildRootSignature()
{
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Create root CBV.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if(errorBlob != nullptr)
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

void LitColumnsApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void LitColumnsApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;

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
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
	}

	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
	}

	for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
	}
	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
	}
	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
	}
	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
	}
	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Normal = torus.Vertices[i].Normal;
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
    const UINT ibByteSize = (UINT)indices.size()  * sizeof(std::uint16_t);

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

void LitColumnsApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
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
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));
}

void LitColumnsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void LitColumnsApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::LightGray);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::DarkGray);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "greenMat";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.2f;

	auto brownMat = std::make_unique<Material>();
	brownMat->Name = "brownMat";
	brownMat->MatCBIndex = 4;
	brownMat->DiffuseSrvHeapIndex = 4;
	brownMat->DiffuseAlbedo = XMFLOAT4(Colors::SaddleBrown);
	brownMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	brownMat->Roughness = 0.3f;
	
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["greenMat"] = std::move(tile0);
	mMaterials["brownMat"] = std::move(brownMat);
}

void LitColumnsApp::BuildRenderItems()
{
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(400.0f, 1.0, 400.0f));
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gridRitem->ObjCBIndex = objectIndex++;
	gridRitem->Mat = mMaterials["greenMat"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	BuildWalls();
	BuildTowers();
	BuildRailings();
	BuildInner();

	// All the render items are opaque.
	for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void LitColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void LitColumnsApp::BuildWalls() {
	//gates
	auto gateLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gateLeft->World, XMMatrixScaling(1.0f, 14.0f, 18.0f) 
		* XMMatrixRotationY(XMConvertToRadians(90.0f))
		* XMMatrixTranslation(77.0f, 7.0f, -15.65f));
	XMStoreFloat4x4(&gateLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gateLeft->ObjCBIndex = objectIndex++;
	gateLeft->Mat = mMaterials["brownMat"].get();
	gateLeft->Geo = mGeometries["shapeGeo"].get();
	gateLeft->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gateLeft->IndexCount = gateLeft->Geo->DrawArgs["box"].IndexCount;
	gateLeft->StartIndexLocation = gateLeft->Geo->DrawArgs["box"].StartIndexLocation;
	gateLeft->BaseVertexLocation = gateLeft->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gateLeft));

	auto gateRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gateRight->World, XMMatrixScaling(1.0f, 14.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90.0f))
		* XMMatrixTranslation(77.0f, 7.0f, 15.65f));
	XMStoreFloat4x4(&gateRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gateRight->ObjCBIndex = objectIndex++;
	gateRight->Mat = mMaterials["brownMat"].get();
	gateRight->Geo = mGeometries["shapeGeo"].get();
	gateRight->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gateRight->IndexCount = gateRight->Geo->DrawArgs["box"].IndexCount;
	gateRight->StartIndexLocation = gateRight->Geo->DrawArgs["box"].StartIndexLocation;
	gateRight->BaseVertexLocation = gateRight->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gateRight));


	//walls
	auto wallLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallLeft->World, XMMatrixScaling(100.0f, 16.0f, 18.0f) * XMMatrixTranslation(0.0f, 8.0f, -59.0f));
	XMStoreFloat4x4(&wallLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallLeft->ObjCBIndex = objectIndex++;
	wallLeft->Mat = mMaterials["stone0"].get();
	wallLeft->Geo = mGeometries["shapeGeo"].get();
	wallLeft->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallLeft->IndexCount = wallLeft->Geo->DrawArgs["box"].IndexCount;
	wallLeft->StartIndexLocation = wallLeft->Geo->DrawArgs["box"].StartIndexLocation;
	wallLeft->BaseVertexLocation = wallLeft->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallLeft));
	
	auto wallRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRight->World, XMMatrixScaling(100.0f, 16.0f, 18.0f)*XMMatrixTranslation(0.0f, 8.0f, 59.0f));
	XMStoreFloat4x4(&wallRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallRight->ObjCBIndex = objectIndex++;
	wallRight->Mat = mMaterials["stone0"].get();
	wallRight->Geo = mGeometries["shapeGeo"].get();
	wallRight->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRight->IndexCount = wallRight->Geo->DrawArgs["box"].IndexCount;
	wallRight->StartIndexLocation = wallRight->Geo->DrawArgs["box"].StartIndexLocation;
	wallRight->BaseVertexLocation = wallRight->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallRight));
	
	auto wallBack = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallBack->World,XMMatrixScaling(100.0f, 16.0f, 18.0f) 
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(-59.0f, 8.0f, 0.0f));
	XMStoreFloat4x4(&wallBack->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallBack->ObjCBIndex = objectIndex++;
	wallBack->Mat = mMaterials["stone0"].get();
	wallBack->Geo = mGeometries["shapeGeo"].get();
	wallBack->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallBack->IndexCount = wallBack->Geo->DrawArgs["box"].IndexCount;
	wallBack->StartIndexLocation = wallBack->Geo->DrawArgs["box"].StartIndexLocation;
	wallBack->BaseVertexLocation = wallBack->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallBack));
	
	auto wallFrontL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontL->World, XMMatrixScaling(35.0f, 16.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(59.0f, 8.0f, -32.5f));
	XMStoreFloat4x4(&wallFrontL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontL->ObjCBIndex = objectIndex++;
	wallFrontL->Mat = mMaterials["stone0"].get();
	wallFrontL->Geo = mGeometries["shapeGeo"].get();
	wallFrontL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontL->IndexCount = wallFrontL->Geo->DrawArgs["box"].IndexCount;
	wallFrontL->StartIndexLocation = wallFrontL->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontL->BaseVertexLocation = wallFrontL->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallFrontL));

	auto wallFrontR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontR->World, XMMatrixScaling(35.0f, 16.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(59.0f, 8.0f, 32.5f));
	XMStoreFloat4x4(&wallFrontR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontR->ObjCBIndex = objectIndex++;
	wallFrontR->Mat = mMaterials["stone0"].get();
	wallFrontR->Geo = mGeometries["shapeGeo"].get();
	wallFrontR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontR->IndexCount = wallFrontR->Geo->DrawArgs["box"].IndexCount;
	wallFrontR->StartIndexLocation = wallFrontR->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontR->BaseVertexLocation = wallFrontR->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallFrontR));
	
	auto wallFrontM = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallFrontM->World, XMMatrixScaling(35.0f, 2.0f, 18.0f)
		* XMMatrixRotationY(XMConvertToRadians(90))
		* XMMatrixTranslation(59.0f, 15.0f, 0.0f));
	XMStoreFloat4x4(&wallFrontM->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallFrontM->ObjCBIndex = objectIndex++;
	wallFrontM->Mat = mMaterials["stone0"].get();
	wallFrontM->Geo = mGeometries["shapeGeo"].get();
	wallFrontM->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallFrontM->IndexCount = wallFrontM->Geo->DrawArgs["box"].IndexCount;
	wallFrontM->StartIndexLocation = wallFrontM->Geo->DrawArgs["box"].StartIndexLocation;
	wallFrontM->BaseVertexLocation = wallFrontM->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallFrontM));
}

void LitColumnsApp::BuildTowers() {
	//scaling changed from 28, 33, 28 to 20, 33, 20 sinc ethey looked way too 'fat'

	auto cylinderFrontL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderFrontL->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(59.0f, 16.5f, -59.0f));
	XMStoreFloat4x4(&cylinderFrontL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderFrontL->ObjCBIndex = objectIndex++;
	cylinderFrontL->Mat = mMaterials["stone0"].get();
	cylinderFrontL->Geo = mGeometries["shapeGeo"].get();
	cylinderFrontL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderFrontL->IndexCount = cylinderFrontL->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderFrontL->StartIndexLocation = cylinderFrontL->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderFrontL->BaseVertexLocation = cylinderFrontL->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderFrontL));

	auto coneFrontL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneFrontL->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(59.0f, 52.0f, -59.0f));
	XMStoreFloat4x4(&coneFrontL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneFrontL->ObjCBIndex = objectIndex++;
	coneFrontL->Mat = mMaterials["greenMat"].get();
	coneFrontL->Geo = mGeometries["shapeGeo"].get();
	coneFrontL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneFrontL->IndexCount = coneFrontL->Geo->DrawArgs["cone"].IndexCount;
	coneFrontL->StartIndexLocation = coneFrontL->Geo->DrawArgs["cone"].StartIndexLocation;
	coneFrontL->BaseVertexLocation = coneFrontL->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneFrontL));

	auto cylinderFrontR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderFrontR->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(59.0f, 16.5f, 59.0f));
	XMStoreFloat4x4(&cylinderFrontR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderFrontR->ObjCBIndex = objectIndex++;
	cylinderFrontR->Mat = mMaterials["stone0"].get();
	cylinderFrontR->Geo = mGeometries["shapeGeo"].get();
	cylinderFrontR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderFrontR->IndexCount = cylinderFrontR->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderFrontR->StartIndexLocation = cylinderFrontR->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderFrontR->BaseVertexLocation = cylinderFrontR->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderFrontR));

	auto coneFrontR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneFrontR->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(59.0f, 52.0f, 59.0f));
	XMStoreFloat4x4(&coneFrontR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneFrontR->ObjCBIndex = objectIndex++;
	coneFrontR->Mat = mMaterials["greenMat"].get();
	coneFrontR->Geo = mGeometries["shapeGeo"].get();
	coneFrontR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneFrontR->IndexCount = coneFrontR->Geo->DrawArgs["cone"].IndexCount;
	coneFrontR->StartIndexLocation = coneFrontR->Geo->DrawArgs["cone"].StartIndexLocation;
	coneFrontR->BaseVertexLocation = coneFrontR->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneFrontR));

	auto cylinderBackR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderBackR->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 16.5f, 59.0f));
	XMStoreFloat4x4(&cylinderBackR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderBackR->ObjCBIndex = objectIndex++;
	cylinderBackR->Mat = mMaterials["stone0"].get();
	cylinderBackR->Geo = mGeometries["shapeGeo"].get();
	cylinderBackR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderBackR->IndexCount = cylinderBackR->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderBackR->StartIndexLocation = cylinderBackR->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderBackR->BaseVertexLocation = cylinderBackR->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderBackR));

	auto coneBackR = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneBackR->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 52.0f, 59.0f));
	XMStoreFloat4x4(&coneBackR->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneBackR->ObjCBIndex = objectIndex++;
	coneBackR->Mat = mMaterials["greenMat"].get();
	coneBackR->Geo = mGeometries["shapeGeo"].get();
	coneBackR->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneBackR->IndexCount = coneBackR->Geo->DrawArgs["cone"].IndexCount;
	coneBackR->StartIndexLocation = coneBackR->Geo->DrawArgs["cone"].StartIndexLocation;
	coneBackR->BaseVertexLocation = coneBackR->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneBackR));

	auto cylinderBackL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderBackL->World, XMMatrixScaling(20.0, 33.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 16.5f, -59.0f));
	XMStoreFloat4x4(&cylinderBackL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderBackL->ObjCBIndex = objectIndex++;
	cylinderBackL->Mat = mMaterials["stone0"].get();
	cylinderBackL->Geo = mGeometries["shapeGeo"].get();
	cylinderBackL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderBackL->IndexCount = cylinderBackL->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderBackL->StartIndexLocation = cylinderBackL->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderBackL->BaseVertexLocation = cylinderBackL->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderBackL));

	auto coneBackL = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneBackL->World, XMMatrixScaling(20.0, 38.0f, 20.0)
		* XMMatrixTranslation(-59.0f, 52.0f, -59.0f));
	XMStoreFloat4x4(&coneBackL->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneBackL->ObjCBIndex = objectIndex++;
	coneBackL->Mat = mMaterials["greenMat"].get();
	coneBackL->Geo = mGeometries["shapeGeo"].get();
	coneBackL->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneBackL->IndexCount = coneBackL->Geo->DrawArgs["cone"].IndexCount;
	coneBackL->StartIndexLocation = coneBackL->Geo->DrawArgs["cone"].StartIndexLocation;
	coneBackL->BaseVertexLocation = coneBackL->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneBackL));

}

void LitColumnsApp::BuildRailings() {
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

void LitColumnsApp::BuildRailAndSpikes(float posX, float posY, float posZ, int dirX, int dirZ){
	auto railFrontO = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&railFrontO->World, XMMatrixScaling(100.0f, 2.0f, 1.0f)
		* XMMatrixRotationY(XMConvertToRadians(90*dirZ))
		* XMMatrixTranslation(posX, posY, posZ));
	XMStoreFloat4x4(&railFrontO->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	railFrontO->ObjCBIndex = objectIndex++;
	railFrontO->Mat = mMaterials["bricks0"].get();
	railFrontO->Geo = mGeometries["shapeGeo"].get();
	railFrontO->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	railFrontO->IndexCount = railFrontO->Geo->DrawArgs["box"].IndexCount;
	railFrontO->StartIndexLocation = railFrontO->Geo->DrawArgs["box"].StartIndexLocation;
	railFrontO->BaseVertexLocation = railFrontO->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(railFrontO));

	for (int i = 0; i < 5; i++) {
		if (i == 0) {
			auto block = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&block->World, XMMatrixScaling(2.0f, 4.0f, 2.0f)
				* XMMatrixTranslation(posX+i*10.0f*dirX, posY+1.0f, posZ+i*10.0f*dirZ));
			XMStoreFloat4x4(&block->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			block->ObjCBIndex = objectIndex++;
			block->Mat = mMaterials["bricks0"].get();
			block->Geo = mGeometries["shapeGeo"].get();
			block->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			block->IndexCount = block->Geo->DrawArgs["box"].IndexCount;
			block->StartIndexLocation = block->Geo->DrawArgs["box"].StartIndexLocation;
			block->BaseVertexLocation = block->Geo->DrawArgs["box"].BaseVertexLocation;
			mAllRitems.push_back(std::move(block));

			auto pyramid = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&pyramid->World, XMMatrixScaling(3.0f, 3.0f, 3.0f)
				* XMMatrixTranslation(posX +i*10.0f*dirX, posY+ 3.0f, posZ +i*10.0f*dirZ));
			XMStoreFloat4x4(&pyramid->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			pyramid->ObjCBIndex = objectIndex++;
			pyramid->Mat = mMaterials["greenMat"].get();
			pyramid->Geo = mGeometries["shapeGeo"].get();
			pyramid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			pyramid->IndexCount = pyramid->Geo->DrawArgs["pyramid"].IndexCount;
			pyramid->StartIndexLocation = pyramid->Geo->DrawArgs["pyramid"].StartIndexLocation;
			pyramid->BaseVertexLocation = pyramid->Geo->DrawArgs["pyramid"].BaseVertexLocation;
			mAllRitems.push_back(std::move(pyramid));
		}
		else {
			auto block = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&block->World, XMMatrixScaling(2.0f, 4.0f, 2.0f)
				* XMMatrixTranslation(posX + i*10.0f*dirX, posY + 1.0f, posZ + i*10.0f*dirZ));
			XMStoreFloat4x4(&block->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			block->ObjCBIndex = objectIndex++;
			block->Mat = mMaterials["bricks0"].get();
			block->Geo = mGeometries["shapeGeo"].get();
			block->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			block->IndexCount = block->Geo->DrawArgs["box"].IndexCount;
			block->StartIndexLocation = block->Geo->DrawArgs["box"].StartIndexLocation;
			block->BaseVertexLocation = block->Geo->DrawArgs["box"].BaseVertexLocation;
			mAllRitems.push_back(std::move(block));

			auto pyramid = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&pyramid->World, XMMatrixScaling(3.0f, 3.0f, 3.0f)
				* XMMatrixTranslation(posX + i*10.0f*dirX, posY + 3.0f, posZ + i*10.0f*dirZ));
			XMStoreFloat4x4(&pyramid->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			pyramid->ObjCBIndex = objectIndex++;
			pyramid->Mat = mMaterials["greenMat"].get();
			pyramid->Geo = mGeometries["shapeGeo"].get();
			pyramid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			pyramid->IndexCount = pyramid->Geo->DrawArgs["pyramid"].IndexCount;
			pyramid->StartIndexLocation = pyramid->Geo->DrawArgs["pyramid"].StartIndexLocation;
			pyramid->BaseVertexLocation = pyramid->Geo->DrawArgs["pyramid"].BaseVertexLocation;
			mAllRitems.push_back(std::move(pyramid));

			auto block2 = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&block2->World, XMMatrixScaling(2.0f, 4.0f, 2.0f)
				* XMMatrixTranslation(posX - i*10.0f*dirX, posY + 1.0f, posZ-i*10.0f*dirZ));
			XMStoreFloat4x4(&block2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			block2->ObjCBIndex = objectIndex++;
			block2->Mat = mMaterials["bricks0"].get();
			block2->Geo = mGeometries["shapeGeo"].get();
			block2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			block2->IndexCount = block2->Geo->DrawArgs["box"].IndexCount;
			block2->StartIndexLocation = block2->Geo->DrawArgs["box"].StartIndexLocation;
			block2->BaseVertexLocation = block2->Geo->DrawArgs["box"].BaseVertexLocation;
			mAllRitems.push_back(std::move(block2));

			auto pyramid2 = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&pyramid2->World, XMMatrixScaling(3.0f, 3.0f, 3.0f)
				* XMMatrixTranslation(posX - i*10.0f*dirX, posY + 3.0f, posZ - i*10.0f*dirZ));
			XMStoreFloat4x4(&pyramid2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
			pyramid2->ObjCBIndex = objectIndex++;
			pyramid2->Mat = mMaterials["greenMat"].get();
			pyramid2->Geo = mGeometries["shapeGeo"].get();
			pyramid2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			pyramid2->IndexCount = pyramid2->Geo->DrawArgs["pyramid"].IndexCount;
			pyramid2->StartIndexLocation = pyramid2->Geo->DrawArgs["pyramid"].StartIndexLocation;
			pyramid2->BaseVertexLocation = pyramid2->Geo->DrawArgs["pyramid"].BaseVertexLocation;
			mAllRitems.push_back(std::move(pyramid2));
		}
	}
}

void LitColumnsApp::BuildInner() {

	auto floor = std::make_unique<RenderItem>();
	floor->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&floor->World, XMMatrixScaling(140.0f, 1.0, 30.0f)
		* XMMatrixTranslation(0.0f, 0.1f, 0.0f));
	XMStoreFloat4x4(&floor->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	floor->ObjCBIndex = objectIndex++;
	floor->Mat = mMaterials["bricks0"].get();
	floor->Geo = mGeometries["shapeGeo"].get();
	floor->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	floor->IndexCount = floor->Geo->DrawArgs["grid"].IndexCount;
	floor->StartIndexLocation = floor->Geo->DrawArgs["grid"].StartIndexLocation;
	floor->BaseVertexLocation = floor->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(floor));

	//pillars
	//xyz = 0, 0, -15, dX of 30
	for (int i = 0; i < 3; i++) {
		auto cylinder = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&cylinder->World, XMMatrixScaling(1.0f, 15.0f, 1.0f)
			* XMMatrixTranslation(-30.0f+30.0f*i, 7.5f, -15.0f));
		XMStoreFloat4x4(&cylinder->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		cylinder->ObjCBIndex = objectIndex++;
		cylinder->Mat = mMaterials["bricks0"].get();
		cylinder->Geo = mGeometries["shapeGeo"].get();
		cylinder->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		cylinder->IndexCount = cylinder->Geo->DrawArgs["cylinder"].IndexCount;
		cylinder->StartIndexLocation = cylinder->Geo->DrawArgs["cylinder"].StartIndexLocation;
		cylinder->BaseVertexLocation = cylinder->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mAllRitems.push_back(std::move(cylinder));

		auto sphere = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&sphere->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)
			* XMMatrixTranslation(-30.0f + 30.0f*i, 16.5f, -15.0f));
		XMStoreFloat4x4(&sphere->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		sphere->ObjCBIndex = objectIndex++;
		sphere->Mat = mMaterials["greenMat"].get();
		sphere->Geo = mGeometries["shapeGeo"].get();
		sphere->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphere->IndexCount = sphere->Geo->DrawArgs["sphere"].IndexCount;
		sphere->StartIndexLocation = sphere->Geo->DrawArgs["sphere"].StartIndexLocation;
		sphere->BaseVertexLocation = sphere->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mAllRitems.push_back(std::move(sphere));


		cylinder = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&cylinder->World, XMMatrixScaling(1.0f, 15.0f, 1.0f)
			* XMMatrixTranslation(-30.0f + 30.0f*i, 7.5f, 15.0f));
		XMStoreFloat4x4(&cylinder->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		cylinder->ObjCBIndex = objectIndex++;
		cylinder->Mat = mMaterials["bricks0"].get();
		cylinder->Geo = mGeometries["shapeGeo"].get();
		cylinder->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		cylinder->IndexCount = cylinder->Geo->DrawArgs["cylinder"].IndexCount;
		cylinder->StartIndexLocation = cylinder->Geo->DrawArgs["cylinder"].StartIndexLocation;
		cylinder->BaseVertexLocation = cylinder->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mAllRitems.push_back(std::move(cylinder));

		sphere = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&sphere->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)
			* XMMatrixTranslation(-30.0f + 30.0f*i, 16.5f, 15.0f));
		XMStoreFloat4x4(&sphere->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		sphere->ObjCBIndex = objectIndex++;
		sphere->Mat = mMaterials["greenMat"].get();
		sphere->Geo = mGeometries["shapeGeo"].get();
		sphere->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphere->IndexCount = sphere->Geo->DrawArgs["sphere"].IndexCount;
		sphere->StartIndexLocation = sphere->Geo->DrawArgs["sphere"].StartIndexLocation;
		sphere->BaseVertexLocation = sphere->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mAllRitems.push_back(std::move(sphere));
	}

	//altar

	auto altarLower = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&altarLower->World, XMMatrixScaling(15.0f, 1.0f, 15.0f) * XMMatrixTranslation(-35.0f, 0.6f, 0.0f));
	XMStoreFloat4x4(&altarLower->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	altarLower->ObjCBIndex = objectIndex++;
	altarLower->Mat = mMaterials["bricks0"].get();
	altarLower->Geo = mGeometries["shapeGeo"].get();
	altarLower->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	altarLower->IndexCount = altarLower->Geo->DrawArgs["box"].IndexCount;
	altarLower->StartIndexLocation = altarLower->Geo->DrawArgs["box"].StartIndexLocation;
	altarLower->BaseVertexLocation = altarLower->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(altarLower));


	auto altarUpper = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&altarUpper->World, XMMatrixScaling(11.0f, 1.0f, 11.0f) * XMMatrixTranslation(-35.0f, 1.6f, 0.0f));
	XMStoreFloat4x4(&altarUpper->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	altarUpper->ObjCBIndex = objectIndex++;
	altarUpper->Mat = mMaterials["bricks0"].get();
	altarUpper->Geo = mGeometries["shapeGeo"].get();
	altarUpper->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	altarUpper->IndexCount = altarUpper->Geo->DrawArgs["box"].IndexCount;
	altarUpper->StartIndexLocation = altarUpper->Geo->DrawArgs["box"].StartIndexLocation;
	altarUpper->BaseVertexLocation = altarUpper->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(altarUpper));

	auto torus = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&torus->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-35.0f, 3.8f, 0.0f));
	XMStoreFloat4x4(&torus->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	torus->ObjCBIndex = objectIndex++;
	torus->Mat = mMaterials["greenMat"].get();
	torus->Geo = mGeometries["shapeGeo"].get();
	torus->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	torus->IndexCount = torus->Geo->DrawArgs["torus"].IndexCount;
	torus->StartIndexLocation = torus->Geo->DrawArgs["torus"].StartIndexLocation;
	torus->BaseVertexLocation = torus->Geo->DrawArgs["torus"].BaseVertexLocation;
	mAllRitems.push_back(std::move(torus));
}