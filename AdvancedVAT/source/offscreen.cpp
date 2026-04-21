#include "stdafx.h"
#include "types.h"
#include "offscreen.h"

struct OffscreenDesc {
	UINT					width, height, div;
	ID3D12DescriptorHeap	*rtvHeap, *srvHeap;	// ヒープを一元管理で切り出してハンドルだけ持つやり方もある (高速)
	ID3D12Resource			*resource;
	D3D12_RESOURCE_STATES	currentState;
	D3D12_CLEAR_VALUE		clearValue;
};

OffscreenDesc *CreateOffscreen(ID3D12Device *pDevice, UINT width, UINT height, UINT div, const D3D12_CLEAR_VALUE &clear, HRESULT &hr)
{
	// SRV Heap
	ID3D12DescriptorHeap *srvHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	if(FAILED(hr = pDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)))) return nullptr;

	// RTV Heap
	ID3D12DescriptorHeap *rtvHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if(FAILED(hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap)))) {
		SAFE_RELEASE(srvHeap);
		return nullptr;
	}

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Alignment = 0;
	resDesc.Width = width / div;
	resDesc.Height = height / div;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.Format = clear.Format;
	resDesc.SampleDesc.Count = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 1;
	heapProp.VisibleNodeMask = 1;

	ID3D12Resource *resource = nullptr;

	hr = pDevice->CreateCommittedResource(
		&heapProp, D3D12_HEAP_FLAG_NONE,
		&resDesc, D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clear,
		IID_PPV_ARGS(&resource));
	if(FAILED(hr)) {
		SAFE_RELEASE(srvHeap);
		SAFE_RELEASE(rtvHeap);
		return nullptr;
	}

	// RTV
	auto rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();	// handle.ptr にオフセットすると複数作れる
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = clear.Format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	pDevice->CreateRenderTargetView(resource, &rtvDesc, rtvHandle);

	// SRV
	auto srvHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();	// handle.ptr にオフセットすると複数作れる
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = clear.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	pDevice->CreateShaderResourceView(resource, &srvDesc, srvHandle);

	auto offscreen = new OffscreenDesc;
	offscreen->width = width / div;
	offscreen->height = height / div;
	offscreen->div = div;
	offscreen->resource = resource;
	offscreen->rtvHeap = rtvHeap;
	offscreen->srvHeap = srvHeap;
	offscreen->currentState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	offscreen->clearValue = clear;

	return offscreen;
}

void ReleaseOffscreen(OffscreenDesc *desc)
{
	SAFE_RELEASE(desc->rtvHeap);
	SAFE_RELEASE(desc->rtvHeap);
	SAFE_RELEASE(desc->resource);
	delete desc;
}


UINT OffscreenUtil::Width() const
{
	return pDesc->width;
}

UINT OffscreenUtil::Height() const
{
	return pDesc->height;
}

ID3D12Resource *OffscreenUtil::Buffer() const
{
	return pDesc->resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE OffscreenUtil::RtvCpuHandle() const
{
	return pDesc->rtvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE OffscreenUtil::SrvGpuHandle() const
{
	return pDesc->srvHeap->GetGPUDescriptorHandleForHeapStart();
}

void OffscreenUtil::Begin(ID3D12GraphicsCommandList *cmd, const D3D12_CPU_DESCRIPTOR_HANDLE *dsv) const
{
	// render target へ遷移
	Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvCpuHandle();
	// output merge stage
	cmd->OMSetRenderTargets(1, &rtv, FALSE, dsv);

	cmd->ClearRenderTargetView(rtv, pDesc->clearValue.Color, 0, nullptr);
	if(dsv) {
		cmd->ClearDepthStencilView(*dsv, D3D12_CLEAR_FLAG_DEPTH, pDesc->clearValue.DepthStencil.Depth, 0, 0, nullptr);
	}
}

void OffscreenUtil::Begin(ID3D12GraphicsCommandList *cmd, const D3D12_CPU_DESCRIPTOR_HANDLE *rtv, const D3D12_CPU_DESCRIPTOR_HANDLE *dsv) const
{
	// render target へ遷移
	Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { *rtv, RtvCpuHandle() };

	// output merge stage
	cmd->OMSetRenderTargets(2, rtvs, FALSE, dsv);

	cmd->ClearRenderTargetView(RtvCpuHandle(), pDesc->clearValue.Color, 0, nullptr);
	if(dsv) {
		cmd->ClearDepthStencilView(*dsv, D3D12_CLEAR_FLAG_DEPTH, pDesc->clearValue.DepthStencil.Depth, 0, 0, nullptr);
	}
}

void OffscreenUtil::End(ID3D12GraphicsCommandList *cmd) const
{
	// 読み取り用 SRV へ
	Transition(cmd, D3D12_RESOURCE_STATE_GENERIC_READ);
	//Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void OffscreenUtil::Transition(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES to) const
{
	if(pDesc->currentState == to) return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = pDesc->resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;	// mip 毎で分けたいなど特殊な状況じゃなければ ALL で
	barrier.Transition.StateBefore = pDesc->currentState;
	barrier.Transition.StateAfter = to;
	cmd->ResourceBarrier(1, &barrier);

	pDesc->currentState = to;
}

void OffscreenUtil::CopySameResource(ID3D12GraphicsCommandList *cmd, ID3D12Resource *src)
{
	Transition(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
	cmd->CopyResource(pDesc->resource, src);
	Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

DXGI_FORMAT OffscreenUtil::Format() const
{
	return pDesc->clearValue.Format;
}

const D3D12_CLEAR_VALUE &OffscreenUtil::ClearValue() const
{
	return pDesc->clearValue;
}

UINT OffscreenUtil::Div() const
{
	return pDesc->div;
}
