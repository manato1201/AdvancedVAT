#pragma once
#include <d3d12.h>

// 教材として不要な時は切り離しやすいように pImpl に近い設計としている
struct ShadowMapDesc;


class ShadowMapUtil {
public:
	ShadowMapUtil() = delete;
	ShadowMapUtil(ShadowMapDesc *desc) : pDesc{desc} {}

	UINT Size() const;
	ID3D12Resource *Buffer() const;

	D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const;

	void Begin(ID3D12GraphicsCommandList *cmd, float clearValue) const;
	void End(ID3D12GraphicsCommandList *cmd) const;

protected:
	void Transition(ID3D12GraphicsCommandList *cmd, D3D12_RESOURCE_STATES to) const;

private:
	ShadowMapDesc	*pDesc;
};
