#include "stdafx.h"
#include "primitive.h"
#include "dx12_util.h"
#include "root_signature.h"

//#define DEBUG_SIGNAMTURE_NAME
#ifdef DEBUG_SIGNAMTURE_NAME
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

SimpleObject::SimpleObject(D3D12_PRIMITIVE_TOPOLOGY topology, DrawFunc func, RootSignature *signature, ID3D12PipelineState *pso, ID3D12PipelineState *psoDepth) noexcept
	: SimpleResourceSet{topology}
	, m_drawFunc(func)
	, m_signature(signature->Signature())
	, m_signatureShadow(nullptr)
	, m_pso(pso)
	, m_psoDepth(psoDepth)
	, m_psoShadow(nullptr)
	, m_ignoreCamera(false)
	, m_useWorld(false)
	, m_hide(false)
	, m_castShadow(false)
	, m_receiveShadow(false)
{
}

void SimpleObject::AttachShadow(ID3D12RootSignature *signature, ID3D12PipelineState *pso)
{
	m_signatureShadow = signature;
	m_psoShadow = pso;
}

void SimpleObject::SetScale(const float3 &scale)
{
	// とりま回転は考慮してない
	world[0][0] = scale[0];
	world[1][1] = scale[1];
	world[2][2] = scale[2];
}

void SimpleObject::SetPosition(const float3 &pos)
{
	world[3][0] = pos[0];
	world[3][1] = pos[1];
	world[3][2] = pos[2];
}

void SimpleObject::UpdateMatricesForConstantBuffer(UINT width, UINT height, SimpleCamera &camera)
{
	if(!m_ignoreCamera) {
		UpdateMatrices(width, height, camera);
	} else if(m_useWorld) {
		Dx12Util::Transpose(matrices.world, world);
	}
}

void SimpleObject::SetConstantBufferVS(ShadowMapDesc *shadowMapDesc, SimpleCamera &light)
{
	if(IsReceiveShadow()) {
		UpdateMatrices(shadowMapDesc, light);
	}
}

void SimpleObject::SetShadowBias(float bias)
{
	shadowParams.bias = DirectX::XMVectorSet(bias, 0, 0, 0);
}

void SimpleObject::SetConstantBufferPS(const float3 &lightPos, const float3 &diffuseColor)
{
	pixelLightingSet.lightPos = DirectX::XMVectorSet(lightPos[0], lightPos[1], lightPos[2], 1);
	pixelLightingSet.diffuseColor = DirectX::XMVectorSet(diffuseColor[0], diffuseColor[1], diffuseColor[2], 1);
}

// 毎回 SetDescriptorHeaps, SetGraphicsRootDescriptorTable しているのはシェーダー可視ヒープを共通化してないから
void SimpleObject::DrawCommand(ID3D12GraphicsCommandList *list)
{
#ifdef DEBUG_SIGNAMTURE_NAME
	UINT size = 0;
	m_signature->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, nullptr);
	if(size > 0) {
		std::vector<wchar_t> wname(size / sizeof(wchar_t));
		if (SUCCEEDED(m_signature->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, wname.data()))) {
			OutputDebugStringW((std::wstring(L"Name: ") + wname.data() + L"\n").c_str());
		}
	}
	if(m_signatureShadow) {
		size = 0;
		m_signatureShadow->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, nullptr);
		if(size > 0) {
			std::vector<wchar_t> wname(size / sizeof(wchar_t));
			if (SUCCEEDED(m_signatureShadow->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, wname.data()))) {
				OutputDebugStringW((std::wstring(L"Name: ") + wname.data() + L"\n").c_str());
			}
		}
	}
#endif

	// todo: RootParameterIndex を直接書いてるのなんとかならん？
	switch(m_drawFunc) {
	case DrawFunc::TEX:
		{
			list->SetGraphicsRootSignature(m_signature);
			list->SetPipelineState(m_pso);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferVS->GetGPUVirtualAddress());
			// texture
			ID3D12DescriptorHeap *heaps[] = { pTextureHeap, pSamplerHeap };
			list->SetDescriptorHeaps(_countof(heaps), heaps);
			list->SetGraphicsRootDescriptorTable(1, heaps[0]->GetGPUDescriptorHandleForHeapStart());
			list->SetGraphicsRootDescriptorTable(2, heaps[1]->GetGPUDescriptorHandleForHeapStart());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		}
		break;

	case DrawFunc::LIT:
		if(IsReceiveShadow()) {
			list->SetGraphicsRootSignature(m_signatureShadow);
			list->SetPipelineState(m_psoShadow);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferVS->GetGPUVirtualAddress());
			list->SetGraphicsRootConstantBufferView(1, pConstantBufferPS->GetGPUVirtualAddress());
			list->SetGraphicsRootConstantBufferView(2, pConstantBufferForShadowPS->GetGPUVirtualAddress());
			// depth texture
			ID3D12DescriptorHeap *heaps[] = { pTextureHeap, pSamplerHeap };
			list->SetDescriptorHeaps(_countof(heaps), heaps);
			list->SetGraphicsRootDescriptorTable(3, heaps[0]->GetGPUDescriptorHandleForHeapStart());
			list->SetGraphicsRootDescriptorTable(4, heaps[1]->GetGPUDescriptorHandleForHeapStart());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		} else {
			list->SetGraphicsRootSignature(m_signature);
			list->SetPipelineState(m_pso);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferVS->GetGPUVirtualAddress());
			list->SetGraphicsRootConstantBufferView(1, pConstantBufferPS->GetGPUVirtualAddress());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		}
		break;

	case DrawFunc::LIT_TEX:
		{
			list->SetGraphicsRootSignature(m_signature);
			list->SetPipelineState(m_pso);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferVS->GetGPUVirtualAddress());
			list->SetGraphicsRootConstantBufferView(1, pConstantBufferPS->GetGPUVirtualAddress());
			// texture
			ID3D12DescriptorHeap *heaps[] = { pTextureHeap, pSamplerHeap };
			list->SetDescriptorHeaps(_countof(heaps), heaps);
			list->SetGraphicsRootDescriptorTable(2, heaps[0]->GetGPUDescriptorHandleForHeapStart());
			list->SetGraphicsRootDescriptorTable(3, heaps[1]->GetGPUDescriptorHandleForHeapStart());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		}
		break;

	case DrawFunc::NV:
		list->SetGraphicsRootSignature(m_signature);
		list->SetPipelineState(m_pso);
		// constant buffer
		list->SetGraphicsRootConstantBufferView(0, pConstantBufferVS->GetGPUVirtualAddress());
		// input assembler
		SetupInputAssembler(list);

		list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		break;

	case DrawFunc::CONSTANT:
		list->SetGraphicsRootSignature(m_signature);
		list->SetPipelineState(m_pso);
		// constant buffer
		list->SetGraphicsRootConstantBufferView(0, pConstantBufferVS->GetGPUVirtualAddress());
		list->SetGraphicsRootConstantBufferView(1, pConstantBufferPS->GetGPUVirtualAddress());
		// input assembler
		SetupInputAssembler(list);

		list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		break;

	case DrawFunc::COPYBACK:
	{
		list->SetGraphicsRootSignature(m_signature);
		list->SetPipelineState(m_pso);
		// constant buffer
		list->SetGraphicsRootConstantBufferView(0, pConstantBufferVS->GetGPUVirtualAddress());
		list->SetGraphicsRootConstantBufferView(1, pConstantBufferPS->GetGPUVirtualAddress());
		// texture
		ID3D12DescriptorHeap *heaps[] = { pTextureHeap, pSamplerHeap };
		list->SetDescriptorHeaps(_countof(heaps), heaps);
		list->SetGraphicsRootDescriptorTable(2, heaps[0]->GetGPUDescriptorHandleForHeapStart());
		list->SetGraphicsRootDescriptorTable(3, heaps[1]->GetGPUDescriptorHandleForHeapStart());
		// input assembler
		SetupInputAssembler(list);

		list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
	}
	break;

	default:
		assert(false);
	}
}

void SimpleObject::DrawDepthCommand(ID3D12GraphicsCommandList *list)
{
	// TODO: RootParameterIndex を直接書いてるのがイヤだな
	// todo: pixel shader が無いので処理は全部同じなのでは..
	switch(m_drawFunc) {
	case DrawFunc::TEX:
		if(m_psoDepth) {
			list->SetGraphicsRootSignature(m_signature);
			list->SetPipelineState(m_psoDepth);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferForDepthVS->GetGPUVirtualAddress());
			// texture
			//ID3D12DescriptorHeap *heaps[] = { pTextureHeap, pSamplerHeap };
			//list->SetDescriptorHeaps(_countof(heaps), heaps);
			//list->SetGraphicsRootDescriptorTable(1, heaps[0]->GetGPUDescriptorHandleForHeapStart());
			//list->SetGraphicsRootDescriptorTable(2, heaps[1]->GetGPUDescriptorHandleForHeapStart());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		}
		break;

	case DrawFunc::LIT:
		if(m_psoDepth) {
			list->SetGraphicsRootSignature(m_signature);
			list->SetPipelineState(m_psoDepth);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferForDepthVS->GetGPUVirtualAddress());
			//list->SetGraphicsRootConstantBufferView(1, pConstantBufferPS->GetGPUVirtualAddress());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		}
		break;

	case DrawFunc::LIT_TEX:
		if(m_psoDepth) {
			list->SetGraphicsRootSignature(m_signature);
			list->SetPipelineState(m_psoDepth);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferForDepthVS->GetGPUVirtualAddress());
			//list->SetGraphicsRootConstantBufferView(1, pConstantBufferPS->GetGPUVirtualAddress());
			// texture
			//ID3D12DescriptorHeap *heaps[] = { pTextureHeap, pSamplerHeap };
			//list->SetDescriptorHeaps(_countof(heaps), heaps);
			//list->SetGraphicsRootDescriptorTable(1, heaps[0]->GetGPUDescriptorHandleForHeapStart());
			//list->SetGraphicsRootDescriptorTable(2, heaps[1]->GetGPUDescriptorHandleForHeapStart());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		}
		break;

	case DrawFunc::NV:
		if(m_psoDepth) {
			list->SetGraphicsRootSignature(m_signature);
			list->SetPipelineState(m_psoDepth);
			// constant buffer
			list->SetGraphicsRootConstantBufferView(0, pConstantBufferForDepthVS->GetGPUVirtualAddress());
			// input assembler
			SetupInputAssembler(list);

			list->DrawIndexedInstanced(IndexCount(), 1, 0, 0, 0);
		}
		break;

	default:
		assert(false);
	}
}


HRESULT SimpleObject::CreateLineStrip(
	Dx12Util *pDx12,
	RootSignature *rootSignature,
	ID3D12PipelineState *pipelineState,
	ID3D12PipelineState *pipelineStateDepth,
	const AllocateSet &allocateSet,
	UINT numPoints,
	SimpleObject **result)
{
	// D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ で geometry shader という流れは今のところやる予定は無い
	auto obj = new SimpleObject(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP, DrawFunc::CONSTANT, rootSignature, pipelineState, pipelineStateDepth);
	HRESULT hr = obj->Allocate(pDx12, allocateSet, numPoints);
	if(FAILED(hr)) {
		delete obj;
		return hr;
	}

	std::vector<SimpleLayout::Vertex> vertices;
	std::vector<uint16_t> indices;

	vertices.resize(numPoints);
	indices.resize(numPoints);

	// 頂点データ
	pDx12->Set(obj->pVertexBuffer, vertices.data(), vertices.size() * sizeof(SimpleLayout::Vertex));

	obj->vertexBufferView.BufferLocation = obj->pVertexBuffer->GetGPUVirtualAddress();
	obj->vertexBufferView.StrideInBytes = sizeof(SimpleLayout::Vertex);
	obj->vertexBufferView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(SimpleLayout::Vertex));

	// インデクスデータ
	for(size_t i = 0; i < numPoints; i++) {
		indices[i] = static_cast<uint16_t>(i);
	}
	pDx12->Set(obj->pIndexBuffer, indices.data(), indices.size() * sizeof(uint16_t));
	obj->SetIndexCount(numPoints);

	obj->indexBufferView.BufferLocation = obj->pIndexBuffer->GetGPUVirtualAddress();
	obj->indexBufferView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(uint16_t));
	obj->indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	*result = obj;
	return S_OK;
}


HRESULT SimpleObject::CreatePlane(
	Dx12Util *pDx12,
	RootSignature *rootSignature,
	ID3D12PipelineState *pipelineState,
	ID3D12PipelineState *pipelineStateDepth,
	const AllocateSet &allocateSet,
	SimpleObject **result)
{
	auto obj = new SimpleObject(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, DrawFunc::LIT, rootSignature, pipelineState, pipelineStateDepth);
	HRESULT hr = obj->Allocate(pDx12, allocateSet, 4);
	if(FAILED(hr)) {
		delete obj;
		return hr;
	}

	// 頂点データ
	SimpleLayout::VertexNormal vertices[4] = {
		{DirectX::XMFLOAT3(-1, 0, -1), DirectX::XMFLOAT3(0, 1, 0)},
		{DirectX::XMFLOAT3( 1, 0, -1), DirectX::XMFLOAT3(0, 1, 0)},
		{DirectX::XMFLOAT3(-1, 0,  1), DirectX::XMFLOAT3(0, 1, 0)},
		{DirectX::XMFLOAT3( 1, 0,  1), DirectX::XMFLOAT3(0, 1, 0)},
	};
	pDx12->Set(obj->pVertexBuffer, vertices, sizeof(vertices));

	obj->vertexBufferView.BufferLocation = obj->pVertexBuffer->GetGPUVirtualAddress();
	obj->vertexBufferView.StrideInBytes = sizeof(SimpleLayout::VertexNormal);
	obj->vertexBufferView.SizeInBytes = sizeof(vertices);

	// インデクスデータ
	uint16_t indices[] = {0, 1, 2, 3};
	pDx12->Set(obj->pIndexBuffer, indices, sizeof(indices));
	obj->SetIndexCount(_countof(indices));

	obj->indexBufferView.BufferLocation = obj->pIndexBuffer->GetGPUVirtualAddress();
	obj->indexBufferView.SizeInBytes = sizeof(indices);
	obj->indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	*result = obj;
	return S_OK;
}
