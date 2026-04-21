#pragma once
#pragma warning (disable: 4005)
#include <d3d12.h>
#include <dxgi1_4.h>
#include <strsafe.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <map>
//#include <windef.h>
#include "types.h"

// この define はシェーダーに届かないのでシェーダーでデプスを使う場合は注意
#define	REVERSED_Z
#ifdef REVERSED_Z
#define	DEPTH_CLEAR	0.0f
#else
#define	DEPTH_CLEAR	1.0f
#endif

struct ShadowMapDesc;
struct OffscreenDesc;
class FrameContext;
class SimpleTexture;

class Dx12Util {
public:
	Dx12Util(UINT frames = 2, DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT dsv_format = DXGI_FORMAT_D32_FLOAT) noexcept;

	virtual ~Dx12Util();

	HRESULT		Initialize(HWND hWnd, UINT width, UINT height, bool debug = false);
	void		Resize(HWND hWnd, UINT width, UINT height);

	HRESULT	CreateShadowMap(UINT size = 4096);
	HRESULT	CreateOffscreen(const std::string &name, UINT width, UINT height, UINT div, const D3D12_CLEAR_VALUE &clearValue);
	HRESULT	CreateOffscreen(const std::string &name, UINT width, UINT height, UINT div = 1, float r = 0, float g = 0, float b = 0, float a = 0);
	HRESULT	CreateOffscreen(const std::string &name, UINT div = 1, float r = 0, float g = 0, float b = 0, float a = 0);

	OffscreenDesc *GetOffscreen(const std::string &name);

	void CommitTexture(SimpleTexture *texture);

	void Begin(float R = 0, float G = 0, float B = 0, float A = 1, float depthClear = DEPTH_CLEAR);
	void ResetTarget();
	void End();

private:
	HRESULT		ImGuiInitialize(HWND hWnd);
	void		ImGuiBegin();
	void		ImGuiEnd();
public:
	void		DrawText(float x, float y, const char *str, float size = 1.0f, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);

protected:
	HRESULT		CreateSwapChain(HWND hWnd, UINT width, UINT height);
	HRESULT		CreateRTV();						// render target view
	HRESULT		CreateDSV(UINT width, UINT height);	// depth stencil view

private:
	const UINT					m_frameCount;
	const DXGI_FORMAT			m_rtvFormat;
	const DXGI_FORMAT			m_dsvFormat;
	UINT						m_frameIndex;
	UINT						m_width, m_height;

	ID3D12Device				*m_pDevice;

	ID3D12CommandQueue			*m_pCommandQueue;
	//ID3D12CommandAllocator		*m_pCommandAllocator;
	ID3D12GraphicsCommandList	*m_pCommandList;

	ID3D12Fence					*m_pFence;
	HANDLE						m_fenceEvent;
	UINT64						m_fenceNextValue;

	FrameContext				*m_frameContext;

	IDXGIFactory4				*m_pFactory;

	IDXGISwapChain3				*m_pSwapChain;

	ID3D12DescriptorHeap		*m_pRtvHeap;
	ID3D12DescriptorHeap		*m_pDsvHeap;
	ID3D12DescriptorHeap		*m_pImguiSrvHeap;

	std::vector<ID3D12Resource *>	m_renderTargets;
	ID3D12Resource					*m_pDepthBuffer;

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>	m_handleRtv;
	D3D12_CPU_DESCRIPTOR_HANDLE					m_handleDsv;

	D3D12_RESOURCE_STATES		m_currentState;	// for back buffer

	ShadowMapDesc							*m_pShadow;
	std::map<std::string, OffscreenDesc *>	m_offscreenMap;	// お好みで std::unique_ptr<OffscreenDesc> とか
	std::vector<OffscreenDesc *>			m_offscreenEntry;

	std::vector<SimpleTexture *>			m_textureEntry;

	struct RESIZE_PARAMS {
		HWND	hWnd;
		UINT	width, height;
		RESIZE_PARAMS(HWND wnd, UINT w, UINT h) : hWnd(wnd), width(w), height(h) {}
	};

	std::vector<RESIZE_PARAMS> m_resizeRequest;

	HRESULT		Resize();

public:
	inline ID3D12Device *Device() const { return m_pDevice; }

	inline ID3D12GraphicsCommandList *CommandList() const { return m_pCommandList; }

	inline const D3D12_CPU_DESCRIPTOR_HANDLE &Rtv() { return m_handleRtv[m_frameIndex]; }
	inline const D3D12_CPU_DESCRIPTOR_HANDLE &Dsv() { return m_handleDsv; }

	inline UINT Width() const { return m_width; }
	inline UINT Height() const { return m_height; }

	inline DXGI_FORMAT RtvFormat() const { return m_rtvFormat; }
	inline DXGI_FORMAT DsvFormat() const { return m_dsvFormat; }

	inline ShadowMapDesc *GetShadowMapDesc() const { return m_pShadow; }
	inline bool HasShadowMap() const { return !!m_pShadow; }

	HRESULT GetBuffer(ID3D12Resource **ppBuffer, UINT64 width, UINT height = 1) const;

	void Set(ID3D12Resource *pBuffer, const void *pSource, const size_t size);

	void Barrier(D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);

	void Exec();

	void WaitForGPU();

	void Present();

	void BeginFrame();
	void EndFrame() { Exec(); Present(); }

	ID3D12Resource *BackBuffer() const;
	void Transition(D3D12_RESOURCE_STATES to);

	static void SetDefaultDepthStencilState(D3D12_DEPTH_STENCIL_DESC &desc, BOOL depthEnable = TRUE);
	static void SetDefaultRasterizer(D3D12_RASTERIZER_DESC &desc);
	static void SetBlendState(D3D12_BLEND_DESC &desc, const D3D12_RENDER_TARGET_BLEND_DESC &rtBlend);

	static void Copy(DirectX::XMMATRIX &dst, const float44 src);
	static void Copy(float44 dst, const DirectX::XMMATRIX &src);
	static void Transpose(DirectX::XMMATRIX &dst, const float44 src);
};


// バックバッファコピーの為の状態遷移をスコープにより自動化させる
class BackBufferCopyScope {
public:
	BackBufferCopyScope() = delete;
	BackBufferCopyScope(Dx12Util *pDx12) : m_pDx12(pDx12) {
		m_pDx12->Transition(D3D12_RESOURCE_STATE_COPY_SOURCE);
	}

	~BackBufferCopyScope() {
		m_pDx12->Transition(D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

private:
	Dx12Util	*m_pDx12;
};
