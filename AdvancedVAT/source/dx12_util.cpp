#include "stdafx.h"
#include "dx12_util.h"
#include "shadow.h"
#include "offscreen.h"
#include "texture.h"

//#include <dxgidebug.h>
//#pragma comment(lib, "dxguid.lib")

extern ShadowMapDesc *CreateShadowMap(ID3D12Device *pDevice, UINT mapSize, HRESULT &hr);
extern void ReleaseShadowMap(ShadowMapDesc *desc);

extern OffscreenDesc *CreateOffscreen(ID3D12Device *pDevice, UINT width, UINT height, UINT div, const D3D12_CLEAR_VALUE &clear, HRESULT &hr);
extern void ReleaseOffscreen(OffscreenDesc *desc);

class FrameContext {
public:
	FrameContext()
		: m_pCommandAllocator(nullptr)
		, m_fenceValue(0)
	{}

	~FrameContext() {
		SAFE_RELEASE(m_pCommandAllocator);
	}

	HRESULT Initialize(ID3D12Device *pDevice) {
		// コマンドアロケータ
		return pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocator));
	}

	ID3D12CommandAllocator *Allocator() const { return m_pCommandAllocator; }

	UINT64 FenceValue() const { return m_fenceValue; }
	void SetFenceValue(UINT64 value) { m_fenceValue = value; }

private:
	ID3D12CommandAllocator		*m_pCommandAllocator;
	UINT64						m_fenceValue;
};


// double buffer は frames = 2 で
Dx12Util::Dx12Util(UINT frames, DXGI_FORMAT rtv_format, DXGI_FORMAT dsv_format) noexcept
	: m_frameCount(frames)
	, m_rtvFormat(rtv_format)
	, m_dsvFormat(dsv_format)
	, m_frameIndex(0)
	, m_width(0)
	, m_height(0)
	, m_pDevice(nullptr)
	, m_pCommandQueue(nullptr)
	//, m_pCommandAllocator(nullptr)
	, m_pCommandList(nullptr)
	, m_pFence(nullptr)
	, m_fenceEvent(0)
	, m_fenceNextValue(0)
	//, m_fenceValue(0)
	, m_frameContext(nullptr)
	, m_pFactory(nullptr)
	, m_pSwapChain(nullptr)
	, m_pRtvHeap(nullptr)
	, m_pDsvHeap(nullptr)
	, m_pImguiSrvHeap(nullptr)
	, m_renderTargets{}
	, m_pDepthBuffer(nullptr)
	, m_handleRtv()
	, m_handleDsv()
	, m_currentState(D3D12_RESOURCE_STATE_RENDER_TARGET)
	, m_pShadow(nullptr)
	, m_offscreenMap{}
	, m_offscreenEntry{}
	, m_textureEntry{}
{
}

// デバッグレイヤー使ってるとこのレポート出るがとりま無視
// D3D12 WARNING: Process is terminating. Using simple reporting. Please call ReportLiveObjects() at runtime for standard reporting. [ STATE_CREATION WARNING #0: UNKNOWN]
// D3D12: **BREAK** enabled for the previous message, which was: [ WARNING STATE_CREATION #0: UNKNOWN ]
Dx12Util::~Dx12Util()
{
	WaitForGPU();

	if(m_fenceEvent) {
		CloseHandle(m_fenceEvent);
		m_fenceEvent = nullptr;
	}

	if(m_pShadow) {
		ReleaseShadowMap(m_pShadow);
		m_pShadow = nullptr;
	}

	for(auto &offscreen : m_offscreenMap) {
		ReleaseOffscreen(offscreen.second);
	}
	m_offscreenMap.clear();

	for(UINT i = 0; i < m_frameCount; i++) {
		SAFE_RELEASE(m_renderTargets[i]);
	}
	SAFE_RELEASE(m_pDepthBuffer);

	SAFE_RELEASE(m_pImguiSrvHeap);
	SAFE_RELEASE(m_pDsvHeap);
	SAFE_RELEASE(m_pRtvHeap);
	SAFE_RELEASE(m_pSwapChain);
	SAFE_RELEASE(m_pFactory);
	SAFE_RELEASE(m_pFence);
	SAFE_RELEASE(m_pCommandList);
	//SAFE_RELEASE(m_pCommandAllocator);
	SAFE_RELEASE(m_pCommandQueue);
	SAFE_RELEASE(m_pDevice);
}


HRESULT Dx12Util::Initialize(HWND hWnd, UINT width, UINT height, bool debug)
{
	// デバッグレイヤー
	if(debug) {
		ID3D12Debug *pDebugLayer = nullptr;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugLayer)))) {
			pDebugLayer->EnableDebugLayer();
			pDebugLayer->Release();

			ID3D12Debug1 *dbg1 = nullptr;
			if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg1)))) {
				dbg1->SetEnableGPUBasedValidation(TRUE);					// SRV/DSV の不整合など
				dbg1->SetEnableSynchronizedCommandQueueValidation(TRUE);	// 必要なら
				dbg1->Release();
			}
		}
	}

	// D3D デバイス
	HRESULT hr = D3D12CreateDevice(nullptr,	// default video adapter
							D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice));
	if(FAILED(hr)) {
		return hr;
	}

	// アロケーターは frame count 数用意
	m_frameContext = new FrameContext[m_frameCount];
	for(UINT i = 0; i < m_frameCount; i++) {
		m_frameContext[i].Initialize(m_pDevice);
	}
	// コマンドキュー
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if(FAILED(hr = m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue)))) return hr;
	// コマンドリスト
	if(FAILED(hr = m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frameContext[0].Allocator(), nullptr, IID_PPV_ARGS(&m_pCommandList))))
		return hr;
	m_pCommandList->Close();

	// フェンス
	if(FAILED(hr = m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)))) return hr;

	m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
	if(m_fenceEvent == nullptr) {
		return GetLastError();
	}

	// ファクトリー (factory に swap chain を作らせる)
	if(FAILED(hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_pFactory)))) return hr;

	// スワップチェーン
	if(FAILED(hr = CreateSwapChain(hWnd, width, height))) return hr;

	// RTV ヒープデスクリプタ
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = m_frameCount;		// 2: double buffer
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if(FAILED(hr = m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pRtvHeap)))) return hr;

	if(FAILED(hr = CreateRTV())) return hr;

	// DSV ヒープデスクリプタ
	D3D12_DESCRIPTOR_HEAP_DESC depthDesc = {};
	depthDesc.NumDescriptors = 1;
	depthDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	depthDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if(FAILED(hr = m_pDevice->CreateDescriptorHeap(&depthDesc, IID_PPV_ARGS(&m_pDsvHeap)))) return hr;

	if(FAILED(hr = CreateDSV(width, height))) return hr;

	m_width = width;
	m_height = height;

	if(debug) {
		ID3D12InfoQueue *info = nullptr;
		if(SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(&info)))) {
			info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE); // 必要なら
			info->Release();
		}
	}

	hr = ImGuiInitialize(hWnd);
	return hr;
}

HRESULT Dx12Util::CreateSwapChain(HWND hWnd, UINT width, UINT height)
{
	// スワップチェーン
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = m_frameCount;	// 2: double buffer
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;
	swapChainDesc.BufferDesc.Format = m_rtvFormat;//DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE;

	// CreateSwapChainForHwnd とか数種類ある
	IDXGISwapChain *pSwapChain = nullptr;
	HRESULT hr = m_pFactory->CreateSwapChain(m_pCommandQueue, &swapChainDesc, &pSwapChain);
	if(FAILED(hr)) {
		return hr;
	}

	hr = pSwapChain->QueryInterface(IID_PPV_ARGS(&m_pSwapChain));
	if(FAILED(hr)) {
		return hr;
	}
	m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	pSwapChain->Release();
	return hr;
}

HRESULT Dx12Util::CreateRTV()
{
	HRESULT hr = S_OK;
	// レンダーターゲットビュー
	auto handle = m_pRtvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = m_rtvFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;

	m_renderTargets.resize(m_frameCount);
	m_handleRtv.resize(m_frameCount);

	auto rtvDescHeapSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	for(UINT i = 0; i < m_frameCount; i++) {
		// get back buffer
		if(FAILED(hr = m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])))) return hr;

		// create rtv (第2引数は null でも可)
		m_pDevice->CreateRenderTargetView(m_renderTargets[i], &rtvDesc, handle);
		m_handleRtv[i] = handle;

		// increment the handle
		handle.ptr += rtvDescHeapSize;
	}

	return hr;
}

HRESULT Dx12Util::CreateDSV(UINT width, UINT height)
{
	HRESULT hr = S_OK;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = m_dsvFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	{
		D3D12_HEAP_PROPERTIES heapProp = {};
		heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProp.CreationNodeMask = 1;
		heapProp.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Alignment          = 0;
		resDesc.Width              = width;
		resDesc.Height             = height;
		resDesc.DepthOrArraySize   = 1;
		resDesc.MipLevels          = 0;
		resDesc.Format             = dsvDesc.Format;
		resDesc.SampleDesc.Count   = 1;
		resDesc.SampleDesc.Quality = 0;
		resDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		resDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = resDesc.Format;
		clearValue.DepthStencil.Depth = DEPTH_CLEAR;
		clearValue.DepthStencil.Stencil = 0;

		hr = m_pDevice->CreateCommittedResource(
			&heapProp, D3D12_HEAP_FLAG_NONE,
			&resDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,	// read はしない
			&clearValue,
			IID_PPV_ARGS(&m_pDepthBuffer));
		if(FAILED(hr)) return hr;

		m_handleDsv = m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();
		m_pDevice->CreateDepthStencilView(m_pDepthBuffer, &dsvDesc, m_handleDsv);
	}

	return hr;
}

void Dx12Util::Resize(HWND hWnd, UINT width, UINT height)
{
	// present のタイミングで実行したいので積んでおく
	m_resizeRequest.emplace_back(hWnd, width, height);
}

HRESULT Dx12Util::Resize()
{
	HRESULT hr = S_OK;

	if(m_resizeRequest.empty()) return hr;

	auto req = m_resizeRequest.back();
	m_resizeRequest.clear();

	HWND hWnd = req.hWnd;
	UINT width = req.width;
	UINT height = req.height;

	WaitForGPU();

	//SAFE_RELEASE(m_pSwapChain);
	SAFE_RELEASE(m_pDepthBuffer);
	for(UINT i = 0; i < m_frameCount; i++) {
		SAFE_RELEASE(m_renderTargets[i]);
	}

	//if(FAILED(hr = CreateSwapChain(hWnd, width, height))) return hr;

#if 0// _DEBUG
	{
		IDXGIDebug1 *dbg;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dbg)))) {
			SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
			dbg->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		}
		dbg->Release();
	}
#endif
	if(FAILED(hr = m_pSwapChain->ResizeBuffers(m_frameCount, width, height, m_rtvFormat, 0))) return hr;

	if(FAILED(hr = CreateRTV())) return hr;
	if(FAILED(hr = CreateDSV(width, height))) return hr;

	m_width = width;
	m_height = height;

	// サイズは最初に指定された div に沿う
	for(auto &ofs : m_offscreenMap) {
		auto offscreen = ofs.second;
		for(auto it = m_offscreenEntry.begin(); it != m_offscreenEntry.end(); ++it) {
			if((*it) == offscreen) {
				m_offscreenEntry.erase(it);
				break;
			}
		}

		if(OffscreenUtil offscreen(GetOffscreen(ofs.first)); !offscreen.IsNULL()) {
			(void)CreateOffscreen(ofs.first, width, height, offscreen.Div(), offscreen.ClearValue());
		}
	}

	m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	return S_OK;
}


HRESULT Dx12Util::CreateShadowMap(UINT size)
{
	if(m_pShadow) {
		ReleaseShadowMap(m_pShadow);
		m_pShadow = nullptr;
	}
	HRESULT hr;
	m_pShadow = ::CreateShadowMap(Device(), size, hr);
	return hr;
}

HRESULT	Dx12Util::CreateOffscreen(const std::string &name, UINT width, UINT height, UINT div, const D3D12_CLEAR_VALUE &clearValue)
{
	HRESULT hr = S_OK;
	auto offscreen = ::CreateOffscreen(m_pDevice, width, height, div, clearValue, hr);
	auto it = m_offscreenMap.find(name);
	if(it != m_offscreenMap.end()) {
		ReleaseOffscreen((*it).second);
	}
	m_offscreenMap[name] = offscreen;
	m_offscreenEntry.push_back(offscreen);
	return hr;
}

HRESULT	Dx12Util::CreateOffscreen(const std::string &name, UINT width, UINT height, UINT div, float r, float g, float b, float a)
{
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = m_rtvFormat;
	clearValue.Color[0] = r;
	clearValue.Color[1] = g;
	clearValue.Color[2] = b;
	clearValue.Color[3] = a;
	clearValue.DepthStencil.Depth = DEPTH_CLEAR;
	return CreateOffscreen(name, width, height, div, clearValue);
}

HRESULT	Dx12Util::CreateOffscreen(const std::string &name, UINT div, float r, float g, float b, float a)
{
	return CreateOffscreen(name, m_width, m_height, div, r, g, b, a);
}

OffscreenDesc *Dx12Util::GetOffscreen(const std::string &name)
{
	auto it = m_offscreenMap.find(name);
	return it == m_offscreenMap.end() ? nullptr : (*it).second;
}


void Dx12Util::CommitTexture(SimpleTexture *texture)
{
	m_textureEntry.push_back(texture);
}


void Dx12Util::Begin(float R, float G, float B, float A, float depthClear)
{
	Barrier(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	FLOAT clearColor[4] = {R, G, B, A};
	m_pCommandList->ClearDepthStencilView(Dsv(), D3D12_CLEAR_FLAG_DEPTH, depthClear, 0, 0, nullptr);
	m_pCommandList->ClearRenderTargetView(Rtv(), clearColor, 0, nullptr);

	ResetTarget();

	ImGuiBegin();
}

void Dx12Util::ResetTarget()
{
	// output merge stage
	m_pCommandList->OMSetRenderTargets(1, &Rtv(), TRUE, &Dsv());
}

void Dx12Util::End()
{
	ImGuiEnd();

	Barrier(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}


HRESULT Dx12Util::GetBuffer(ID3D12Resource **ppBuffer, UINT64 width, UINT height) const
{
	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 0;
	heapProp.VisibleNodeMask = 0;

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.Alignment          = 0;
	resDesc.Width              = width;
	resDesc.Height             = height;
	resDesc.DepthOrArraySize   = 1;
	resDesc.MipLevels          = 1;
	resDesc.Format             = DXGI_FORMAT_UNKNOWN;
	resDesc.SampleDesc.Count   = 1;
	resDesc.SampleDesc.Quality = 0;
	resDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;
	resDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;	// これ何だろう

	return m_pDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(ppBuffer));
}

void Dx12Util::Set(ID3D12Resource *pBuffer, const void *pSource, const size_t size)
{
	void *mapped = nullptr;
	auto hr = pBuffer->Map(0, nullptr, &mapped);
	if(SUCCEEDED(hr)) {
		CopyMemory(mapped, pSource, size);
		pBuffer->Unmap(0, nullptr);
	}
}

void Dx12Util::Barrier(D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = m_renderTargets[m_frameIndex];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	m_pCommandList->ResourceBarrier(1, &barrier);
}

void Dx12Util::WaitForGPU()
{
	++m_fenceNextValue;
	m_pCommandQueue->Signal(m_pFence, m_fenceNextValue);
	if(m_pFence->GetCompletedValue() < m_fenceNextValue) {
		m_pFence->SetEventOnCompletion(m_fenceNextValue, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	for(UINT i = 0; i < m_frameCount; i++) {
		const UINT64 fv = m_frameContext[i].FenceValue();
		if(fv && m_pFence->GetCompletedValue() < fv) {
			m_pFence->SetEventOnCompletion(fv, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}
}

void Dx12Util::BeginFrame()
{
	Resize();

	auto &fc = m_frameContext[m_frameIndex];

	// 前フレームを待つ
	if(fc.FenceValue() && m_pFence->GetCompletedValue() < fc.FenceValue()) {
		m_pFence->SetEventOnCompletion(fc.FenceValue(), m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	fc.Allocator()->Reset();
	m_pCommandList->Reset(fc.Allocator(), nullptr);

	// offscreen は安全の為使う前に一度 transition を通しておく
	for(auto offscreenDesc : m_offscreenEntry) {
		if(OffscreenUtil offscreen(offscreenDesc); !offscreen.IsNULL()) {
			offscreen.Begin(m_pCommandList, nullptr);
			offscreen.End(m_pCommandList);
		}
	}
	m_offscreenEntry.clear();

	// 遅延 texture をアップロードする
	for(auto it = m_textureEntry.begin(); it != m_textureEntry.end(); ) {
		if((*it)->UploadTexture(m_pCommandList)) {
			it = m_textureEntry.erase(it);
		} else {
			++it;
		}
	}
}

void Dx12Util::Exec()
{
	if(SUCCEEDED(m_pCommandList->Close())) {
		ID3D12CommandList *const pCommandList = m_pCommandList;
		m_pCommandQueue->ExecuteCommandLists(1, &pCommandList);
	}
}

void Dx12Util::Present()
{
	if(SUCCEEDED(m_pSwapChain->Present(1, 0))) {
		++m_fenceNextValue;
		m_pCommandQueue->Signal(m_pFence, m_fenceNextValue);
		m_frameContext[m_frameIndex].SetFenceValue(m_fenceNextValue);
		m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	}
}

ID3D12Resource *Dx12Util::BackBuffer() const
{
	return m_renderTargets[m_frameIndex];
}

void Dx12Util::Transition(D3D12_RESOURCE_STATES to)
{
	if(m_currentState == to) return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = BackBuffer();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = m_currentState;
	barrier.Transition.StateAfter = to;
	m_pCommandList->ResourceBarrier(1, &barrier);

	m_currentState = to;
}


void Dx12Util::SetDefaultDepthStencilState(D3D12_DEPTH_STENCIL_DESC &desc, BOOL depthEnable)
{
	desc.DepthEnable = depthEnable;
#ifdef REVERSED_Z
	//desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;	// Reversed-Z
	desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;	// Reversed-Z
#else
	desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
#endif
	//desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	desc.StencilEnable = FALSE;
	desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	desc.FrontFace.StencilFailOp      = D3D12_STENCIL_OP_KEEP;
	desc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	desc.FrontFace.StencilPassOp      = D3D12_STENCIL_OP_KEEP;
	desc.FrontFace.StencilFunc        = D3D12_COMPARISON_FUNC_ALWAYS;
	desc.BackFace.StencilFailOp      = D3D12_STENCIL_OP_KEEP;
	desc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	desc.BackFace.StencilPassOp      = D3D12_STENCIL_OP_KEEP;
	desc.BackFace.StencilFunc        = D3D12_COMPARISON_FUNC_ALWAYS;
}

void Dx12Util::SetDefaultRasterizer(D3D12_RASTERIZER_DESC &desc)
{
	desc = {};
	desc.FillMode = D3D12_FILL_MODE_SOLID;
	desc.CullMode = D3D12_CULL_MODE_NONE;
	desc.FrontCounterClockwise = FALSE;
	desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	desc.DepthClipEnable = TRUE;
	desc.MultisampleEnable = FALSE;
	desc.AntialiasedLineEnable = FALSE;
	desc.ForcedSampleCount = 0;
	desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;	// なにこれ
}

void Dx12Util::SetBlendState(D3D12_BLEND_DESC &desc, const D3D12_RENDER_TARGET_BLEND_DESC &rtBlend)
{
	desc = {};
	desc.AlphaToCoverageEnable = FALSE;	// pixel shader で discard する時は false, multi sample する時はここ大事
	desc.IndependentBlendEnable = FALSE;

	if(desc.IndependentBlendEnable) {
		for(int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
			// とりま全部同じで
			desc.RenderTarget[i] = rtBlend;
			/*
			auto &rt = desc.RenderTarget[i];
			rt.BlendEnable = FALSE;
			rt.LogicOpEnable = FALSE;
			rt.SrcBlend = D3D12_BLEND_ONE;
			rt.DestBlend = D3D12_BLEND_ZERO;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt.LogicOp = D3D12_LOGIC_OP_NOOP;
			rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			*/
		}
	} else {
		desc.RenderTarget[0] = rtBlend;
	}
}


void Dx12Util::Copy(DirectX::XMMATRIX &dst, const float44 src)
{
	dst.r[0] = DirectX::XMVectorSet(src[0][0], src[0][1], src[0][2], src[0][3]);
	dst.r[1] = DirectX::XMVectorSet(src[1][0], src[1][1], src[1][2], src[1][3]);
	dst.r[2] = DirectX::XMVectorSet(src[2][0], src[2][1], src[2][2], src[2][3]);
	dst.r[3] = DirectX::XMVectorSet(src[3][0], src[3][1], src[3][2], src[3][3]);
}

void Dx12Util::Copy(float44 dst, const DirectX::XMMATRIX &src)
{
	DirectX::XMFLOAT4X4 m;
	DirectX::XMStoreFloat4x4(&m, src);
	dst[0][0] = m._11;
	dst[0][1] = m._12;
	dst[0][2] = m._13;
	dst[0][3] = m._14;
	dst[1][0] = m._21;
	dst[1][1] = m._22;
	dst[1][2] = m._23;
	dst[1][3] = m._24;
	dst[2][0] = m._31;
	dst[2][1] = m._32;
	dst[2][2] = m._33;
	dst[2][3] = m._34;
	dst[3][0] = m._41;
	dst[3][1] = m._42;
	dst[3][2] = m._43;
	dst[3][3] = m._44;
}

void Dx12Util::Transpose(DirectX::XMMATRIX &dst, const float44 src)
{
	dst.r[0] = DirectX::XMVectorSet(src[0][0], src[1][0], src[2][0], src[3][0]);
	dst.r[1] = DirectX::XMVectorSet(src[0][1], src[1][1], src[2][1], src[3][1]);
	dst.r[2] = DirectX::XMVectorSet(src[0][2], src[1][2], src[2][2], src[3][2]);
	dst.r[3] = DirectX::XMVectorSet(src[0][3], src[1][3], src[2][3], src[3][3]);
}
