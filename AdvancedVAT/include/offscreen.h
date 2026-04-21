#pragma once
#include <d3d12.h>

// 教材として不要な時は切り離しやすいように pImpl に近い設計としている
struct OffscreenDesc;


class OffscreenUtil {
public:
	OffscreenUtil() = delete;
	OffscreenUtil(OffscreenDesc *desc) : pDesc{desc} {}

	bool IsNULL() const { return pDesc == nullptr; }

	UINT Width() const;
	UINT Height() const;
	ID3D12Resource *Buffer() const;

	D3D12_CPU_DESCRIPTOR_HANDLE RtvCpuHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle() const;

	void Begin(ID3D12GraphicsCommandList *cmd, const D3D12_CPU_DESCRIPTOR_HANDLE *dsv) const;
	void Begin(ID3D12GraphicsCommandList *cmd, const D3D12_CPU_DESCRIPTOR_HANDLE *rtv, const D3D12_CPU_DESCRIPTOR_HANDLE *dsv) const;
	void End(ID3D12GraphicsCommandList *cmd) const;

	void CopySameResource(ID3D12GraphicsCommandList *cmd, ID3D12Resource *src);

	void Transition(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES to) const;

	DXGI_FORMAT Format() const;
	const D3D12_CLEAR_VALUE &ClearValue() const;
	UINT Div() const;

private:
	OffscreenDesc	*pDesc;
};

