#include "stdafx.h"
#include "dx12_util.h"
#include "shadow.h"

// 本来は共通のシェーダ可視ヒープに各リソースの SRV を割り当てる方が効率が良い
// この教材ではそこまで詰めない

struct ShadowMapDesc {
	UINT					mapSize;
	ID3D12DescriptorHeap	*srvHeap;
	ID3D12DescriptorHeap	*dsvHeap;
	ID3D12Resource			*depthBuffer;
	D3D12_RESOURCE_STATES	currentState;
};


ShadowMapDesc *CreateShadowMap(ID3D12Device *pDevice, UINT mapSize, HRESULT &hr)
{
	ID3D12DescriptorHeap *srvHeap, *dsvHeap;
	ID3D12Resource *depthBuffer;

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	if(FAILED(hr = pDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)))) return nullptr;

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Alignment = 0;
	resDesc.Width = mapSize;
	resDesc.Height = mapSize;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.Format = DXGI_FORMAT_R32_TYPELESS; // typeless
	resDesc.SampleDesc.Count = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = DEPTH_CLEAR;
	clearValue.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProp.CreationNodeMask = 1;
	heapProp.VisibleNodeMask = 1;

	hr = pDevice->CreateCommittedResource(
		&heapProp, D3D12_HEAP_FLAG_NONE,
		&resDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&depthBuffer));
	if(FAILED(hr)) {
		SAFE_RELEASE(srvHeap);
		return nullptr;
	}

	// デプスステンシルバッファ
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	if(FAILED(hr = pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)))) {
		SAFE_RELEASE(srvHeap);
		SAFE_RELEASE(depthBuffer);
		return nullptr;
	}

	auto srvHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();

	// SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	pDevice->CreateShaderResourceView(depthBuffer, &srvDesc, srvHandle);

	auto dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	pDevice->CreateDepthStencilView(depthBuffer, &dsvDesc, dsvHandle);

	auto shadowDesc = new ShadowMapDesc;
	shadowDesc->mapSize = mapSize;
	shadowDesc->srvHeap = srvHeap;
	shadowDesc->dsvHeap = dsvHeap;
	shadowDesc->depthBuffer = depthBuffer;
	shadowDesc->currentState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	//srvHeap->GetGPUDescriptorHandleForHeapStart();
	depthBuffer->SetName(L"shadowDepth");

	return shadowDesc;
}

void ReleaseShadowMap(ShadowMapDesc *desc)
{
	SAFE_RELEASE(desc->srvHeap);
	SAFE_RELEASE(desc->dsvHeap);
	SAFE_RELEASE(desc->depthBuffer);
	delete desc;
}


UINT ShadowMapUtil::Size() const
{
	return pDesc->mapSize;
}

ID3D12Resource *ShadowMapUtil::Buffer() const
{
	return pDesc->depthBuffer;
}

D3D12_CPU_DESCRIPTOR_HANDLE ShadowMapUtil::Dsv() const
{
	return pDesc->dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void ShadowMapUtil::Begin(ID3D12GraphicsCommandList *cmd, float clearValue) const
{
	// まず DEPTH_WRITE へ
	Transition(cmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	D3D12_CPU_DESCRIPTOR_HANDLE dsv = Dsv();
	// output merge stage (RTV 無し)
	cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

	cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, clearValue, 0, 0, nullptr);
}

void ShadowMapUtil::End(ID3D12GraphicsCommandList *cmd) const
{
	// 読み取り用 SRV へ
	Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void ShadowMapUtil::Transition(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES to) const
{
	if(pDesc->currentState == to) return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = pDesc->depthBuffer;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;	// mip 毎に分けたいなど特殊な状況じゃなければ ALL で
	barrier.Transition.StateBefore = pDesc->currentState;
	barrier.Transition.StateAfter = to;
	cmd->ResourceBarrier(1, &barrier);

	pDesc->currentState = to;
}
