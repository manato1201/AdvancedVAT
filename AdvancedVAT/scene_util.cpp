#include "stdafx.h"
#include "camera.h"
#include "primitive.h"
#include "dx12_util.h"
#include "shadow.h"
#include "scene_util.h"
#include "scene_resources.h"
#include "fbx_load.h"

namespace Scene {

bool InitDefaultPso(Dx12Util *pDx12, const std::string &name, const std::string &sig_name, const std::string &shader_name,
	const D3D12_INPUT_LAYOUT_DESC &layout, PrimtiveType primitiveType, BlendType blendType, DepthUse depth)
{
	RootSignature *signature = Scene::GetSignature(sig_name);
	if(!signature) return false;

	SimpleShader_VS_PS *shader = Scene::GetShader(shader_name);
	if(!shader) return false;

	return AddPso(name, signature->CreateDefaultPipelineState(pDx12, shader, layout, primitiveType, blendType, depth));
}

bool InitDefaultPso2RT(Dx12Util *pDx12, const std::string &name, const std::string &sig_name, const std::string &shader_name,
	const D3D12_INPUT_LAYOUT_DESC &layout, PrimtiveType primitiveType, BlendType blendType, DepthUse depth)
{
	RootSignature *signature = Scene::GetSignature(sig_name);
	if(!signature) return false;

	SimpleShader_VS_PS *shader = Scene::GetShader(shader_name);
	if(!shader) return false;

	return AddPso(name, signature->CreateDefaultPipelineState2RT(pDx12, shader, layout, primitiveType, blendType, depth));
}

bool InitDepthPSO(Dx12Util *pDx12, const std::string &name, const D3D12_INPUT_LAYOUT_DESC &layout, PrimtiveType primitiveType, DepthUse depth)
{
	RootSignature *signature = Scene::GetSignature(name);
	if(!signature) return false;

	// カリカリのパフォーマンスを求めるなら depth 専用の頂点シェーダーを用意するけどそこまでやらない
	SimpleShader_VS_PS *shader = Scene::GetShader(name);
	if(!shader) return false;

	return AddPsoDepth(name, signature->CreateDepthPipelineState(pDx12, shader, layout, primitiveType, depth));
}

void SetupCamera(SimpleCamera &camera, float x, float y, float z, float n, float f, float fovy,	float atx, float aty, float atz, float upx, float upy, float upz)
{
	camera.eye[0] = x;
	camera.eye[1] = y;
	camera.eye[2] = z;
	camera.at[0] = atx;
	camera.at[1] = aty;
	camera.at[2] = atz;
	camera.up[0] = upx;
	camera.up[1] = upy;
	camera.up[2] = upz;
	camera.znear = n;
	camera.zfar = f;
	camera.fovy = fovy;
}

DirectX::XMMATRIX OrthoScreen(UINT width, UINT height)
{
	float w = static_cast<float>(width);
	float h = static_cast<float>(height);

	return DirectX::XMMatrixOrthographicOffCenterLH(
		-w / 2.0f, w / 2.0f,    // left, right
		-h / 2.0f, h / 2.0f,    // bottom, top
		0.0f, 1.0f              // near, far
	);
}

SimpleObject *LoadFBX(Dx12Util *pDx12, ImportDesc &desc, RootSignature *signature, ID3D12PipelineState *pso, ID3D12PipelineState *psoDepth, bool hasTexture, HRESULT &hr)
{
	SimpleObject *obj = nullptr;

	if(!AssImport::LoadFbx(desc)) {
		hr = E_FAIL;
		return nullptr;
	}

	if(!desc.meshes.empty()) {
		DrawFunc func = hasTexture ? DrawFunc::LIT_TEX : DrawFunc::LIT;
		const size_t stride = func == DrawFunc::LIT ? 9 : 11;

		std::vector<float> vertices;
		// 現状１メッシュしか読まない
		const auto &mesh = desc.meshes[0];
		vertices.reserve(mesh.Vertices.size() * stride);
		for(auto &vx : mesh.Vertices) {
			vertices.push_back(vx.Position.x);
			vertices.push_back(vx.Position.y);
			vertices.push_back(vx.Position.z);
			vertices.push_back(vx.Normal.x);
			vertices.push_back(vx.Normal.y);
			vertices.push_back(vx.Normal.z);
			if(func == DrawFunc::LIT_TEX) {
				vertices.push_back(vx.UV.x);
				vertices.push_back(vx.UV.y);
			}
			vertices.push_back(vx.Tangent.x);
			vertices.push_back(vx.Tangent.y);
			vertices.push_back(vx.Tangent.z);
		}

		// fbx は triangle list
		obj = new SimpleObject(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, func, signature, pso, psoDepth);

		hr = obj->Allocate(pDx12,
			sizeof(SimpleConstantBuffer::ProjectionModelviewWorld),
			sizeof(SimpleConstantBuffer::PixelLightingSet),
			static_cast<UINT>(vertices.size() * sizeof(float)),
			static_cast<UINT>(mesh.Indices.size() * sizeof(uint32_t)));
		if(FAILED(hr)) {
			delete obj;
			return nullptr;
		}

		bool receiveShadow = func == DrawFunc::LIT;	// 一旦テクスチャありは影落とさない
		if(receiveShadow) {
			if(FAILED(obj->AllocateForShadow(pDx12, sizeof(SimpleConstantBuffer::PixelShadowParams)))) {
				receiveShadow = false;
			} else {
				obj->SetShadowBias(0.000005f);	// todo: shader が reversed-z 固定
			}
		}
		obj->SetShadow(true, receiveShadow);

		hr = obj->AllocateForDepth(pDx12, sizeof(SimpleConstantBuffer::ProjectionModelviewWorld));
		if(FAILED(hr)) {
			delete obj;
			return nullptr;
		}

		// 頂点データ
		pDx12->Set(obj->pVertexBuffer, &vertices[0], vertices.size() * sizeof(float));
		obj->vertexBufferView.BufferLocation = obj->pVertexBuffer->GetGPUVirtualAddress();
		obj->vertexBufferView.StrideInBytes = static_cast<UINT>(sizeof(float) * stride);
		obj->vertexBufferView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(float));
		// インデクスデータ
		pDx12->Set(obj->pIndexBuffer, mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));
		obj->indexBufferView.BufferLocation = obj->pIndexBuffer->GetGPUVirtualAddress();
		obj->indexBufferView.SizeInBytes = static_cast<UINT>(mesh.Indices.size() * sizeof(uint32_t));
		obj->indexBufferView.Format = DXGI_FORMAT_R32_UINT;
		obj->SetIndexCount(static_cast<UINT>(mesh.Indices.size()));
	}

	return obj;
}

SimpleObject *CreateRectangleObject(Dx12Util *pDx12, RootSignature *signature, ID3D12PipelineState *pipelineState, HRESULT &hr)
{
	if(signature == nullptr || pipelineState == nullptr) {
		hr = E_FAIL;
		return nullptr;
	}

	auto obj = new SimpleObject(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, DrawFunc::COPYBACK, signature, pipelineState);
	hr = obj->Allocate(pDx12,
		sizeof(SimpleConstantBuffer::ProjectionModelviewWorld),	// 定数バッファサイズ
		sizeof(SimpleConstantBuffer::PixelPostParams),			// 定数バッファサイズ
		sizeof(SimpleLayout::VertexTexcoord) * 4,				// 頂点バッファサイズ
		sizeof(uint16_t) * 4									// インデクスサイズ
	);
	if(FAILED(hr)) {
		delete obj;
		return nullptr;
	}

	// 頂点データ
	SimpleLayout::VertexTexcoord vertices[4] = {
		{DirectX::XMFLOAT3(-1, -1, 0), DirectX::XMFLOAT2(0, 1)},
		{DirectX::XMFLOAT3( 1, -1, 0), DirectX::XMFLOAT2(1, 1)},
		{DirectX::XMFLOAT3(-1,  1, 0), DirectX::XMFLOAT2(0, 0)},
		{DirectX::XMFLOAT3( 1,  1, 0), DirectX::XMFLOAT2(1, 0)},
	};
	pDx12->Set(obj->pVertexBuffer, vertices, sizeof(vertices));

	obj->vertexBufferView.BufferLocation = obj->pVertexBuffer->GetGPUVirtualAddress();
	obj->vertexBufferView.StrideInBytes = sizeof(SimpleLayout::VertexTexcoord);
	obj->vertexBufferView.SizeInBytes = sizeof(vertices);

	// インデクスデータ
	uint16_t indices[] = {0, 1, 2, 3};
	pDx12->Set(obj->pIndexBuffer, indices, sizeof(indices));
	obj->SetIndexCount(_countof(indices));

	obj->indexBufferView.BufferLocation = obj->pIndexBuffer->GetGPUVirtualAddress();
	obj->indexBufferView.SizeInBytes = sizeof(indices);
	obj->indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	return obj;
}


HRESULT BindTextureResource(ID3D12Device *pDevice, SimpleObject *obj, ID3D12Resource *textureResource, DXGI_FORMAT format)
{
	HRESULT hr = S_OK;

	if(!obj->pTextureHeap) {
		// shader resource view
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NodeMask = 0;

		if(FAILED(hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&obj->pTextureHeap)))) return hr;
		obj->handleSrv = obj->pTextureHeap->GetCPUDescriptorHandleForHeapStart();
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
	texDesc.Format = format;
	texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	texDesc.Texture2D.MipLevels = 1;
	texDesc.Texture2D.MostDetailedMip = 0;
	texDesc.Texture2D.PlaneSlice = 0;
	texDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	pDevice->CreateShaderResourceView(textureResource, &texDesc, obj->handleSrv);

	if(!obj->pSamplerHeap) {
		// sampler
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NodeMask = 0;
		hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&obj->pSamplerHeap));
		if(SUCCEEDED(hr)) {
			D3D12_SAMPLER_DESC desc = {};
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.MaxLOD = FLT_MAX;
			desc.MinLOD = -FLT_MAX;
			desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			desc.MipLODBias = 0.0f;
			desc.MaxAnisotropy = 0;
			obj->handleSampler = obj->pSamplerHeap->GetCPUDescriptorHandleForHeapStart();
			pDevice->CreateSampler(&desc, obj->handleSampler);
		}
		// todo: D3D12_STATIC_SAMPLER_DESC, D3D12_VERSIONED_ROOT_SIGNATURE_DESC を使って static sampler にする
	}

	return hr;
}

HRESULT BindShadowMapResource(ID3D12Device *pDevice, SimpleObject *obj, ShadowMapDesc *shadowMapDesc)
{
	ShadowMapUtil shadow(shadowMapDesc);

	// depth map を可視化する為の SRV (本来は共通のヒープに SRV をまとめるのが定石)
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;
	HRESULT hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&obj->pTextureHeap));
	if(SUCCEEDED(hr)) {
		D3D12_SHADER_RESOURCE_VIEW_DESC texDesc = {};
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;	// !!
		texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		texDesc.Texture2D.MipLevels = 1;
		texDesc.Texture2D.MostDetailedMip = 0;
		texDesc.Texture2D.PlaneSlice = 0;
		texDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		obj->handleSrv = obj->pTextureHeap->GetCPUDescriptorHandleForHeapStart();
		pDevice->CreateShaderResourceView(shadow.Buffer(), &texDesc, obj->handleSrv);

		// sampler
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NodeMask = 0;
		hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&obj->pSamplerHeap));
		if(SUCCEEDED(hr)) {
			D3D12_SAMPLER_DESC desc = {};
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.MaxLOD = 0;
			desc.MinLOD = 0;
			desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;	// ハードウェア PCF をやらない
			desc.MipLODBias = 0.0f;
			desc.MaxAnisotropy = 0;
			obj->handleSampler = obj->pSamplerHeap->GetCPUDescriptorHandleForHeapStart();
			pDevice->CreateSampler(&desc, obj->handleSampler);
		}
	}

	return hr;
}

void SetViewport(D3D12_VIEWPORT &viewport, D3D12_RECT &scissor, UINT width, UINT height, UINT x, UINT y)
{
	viewport.TopLeftX = static_cast<FLOAT>(x);
	viewport.TopLeftY = static_cast<FLOAT>(y);
	viewport.Width = static_cast<FLOAT>(width);
	viewport.Height = static_cast<FLOAT>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	scissor.left = x;
	scissor.top = y;
	scissor.right = width + x;
	scissor.bottom = height + y;
}

void SetViewport(ID3D12GraphicsCommandList *cmd, UINT width, UINT height, UINT x, UINT y)
{
	D3D12_VIEWPORT	viewport;
	D3D12_RECT		scissor;
	Scene::SetViewport(viewport, scissor, width, height, x, y);
	cmd->RSSetViewports(1, &viewport);
	cmd->RSSetScissorRects(1, &scissor);
}

template<int N> float CalcBezier(float t, const FourPoints &points)
{
	float t1 = 1.0f - t;
	float t2 = t1 * t1;
	float t3 = t2 * t1;
	float a = points.a[N] * t3;
	float b = points.b[N] * t2 * t * 3.0f;
	float c = points.c[N] * t1 * (t * t) * 3.0f;
	float d = points.d[N] * (t * t * t);
	return a + b + c + d;
}

std::pair<float, float> Bezier(float t, const FourPoints &points)
{
	return std::make_pair<float, float>(CalcBezier<0>(t, points), CalcBezier<1>(t, points));
}

} // Scene
