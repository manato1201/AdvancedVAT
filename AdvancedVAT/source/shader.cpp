#include "stdafx.h"
#include "types.h"
#include "shader.h"
#include "camera.h"
#include "dx12_util.h"
#include "primitive.h"
#include "shadow.h"
#include <d3dcompiler.h>

namespace {

std::string WideCharToUTF8(LPCWSTR wstr) {
	if(!wstr) return {};
	int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
	std::string result(size > 0 ? size - 1 : 0, 0);
	if(size > 1) {
		WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, nullptr, nullptr);
	}
	return result;
}

} // unnamed


Dx12Shader::Dx12Shader(LPCWSTR szFileName, LPSTR szFuncName, LPSTR szProfileName)
	: m_code(nullptr)
	, m_path(szFileName)
	, m_state(S_OK)
{
	ID3DBlob *pErrors = nullptr;
	UINT compileFlags = 0;
#ifdef _DEBUG
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	m_state = D3DCompileFromFile(szFileName,
		static_cast<const D3D_SHADER_MACRO *>(nullptr),
		static_cast<ID3DInclude *>(nullptr),
		szFuncName, szProfileName,
		compileFlags, 0, &m_code, &pErrors);
	if(FAILED(m_state)) {
		const char *p = static_cast<char *>(pErrors->GetBufferPointer());
		m_error = p;
	}
}

Dx12Shader::Dx12Shader(LPCWSTR csoFilePath)
	: m_code(nullptr)
	, m_path(csoFilePath)
	, m_state(S_OK)
{
	m_state = D3DReadFileToBlob(csoFilePath, &m_code);
	if(FAILED(m_state)) {
		m_error = "Failed to load .cso file, " + WideCharToUTF8(csoFilePath);
	}
}

Dx12Shader::~Dx12Shader()
{
	SAFE_RELEASE(m_code);
}

bool Dx12Shader::IsValid() const
{
	return (m_code != nullptr) && SUCCEEDED(m_state);
}

D3D12_SHADER_BYTECODE Dx12Shader::ByteCode() const
{
	return { m_code->GetBufferPointer(), m_code->GetBufferSize() };
}

//////////////////////////////////////////////////////////////////////////////

SimpleShader_VS_PS *SimpleShader_VS_PS::Initialize(LPCWSTR vs_path, LPCWSTR ps_path)
{
	auto shaderSet = new SimpleShader_VS_PS();
	if(SUCCEEDED(shaderSet->InitializeInternal(vs_path, ps_path))) {
		return shaderSet;
	}
	delete shaderSet;
	return nullptr;
}

SimpleShader_VS_PS::SimpleShader_VS_PS() noexcept
	: vs{}
	, ps{}
	, pso{}
{
}

SimpleShader_VS_PS::~SimpleShader_VS_PS()
{
	Release();
}

void SimpleShader_VS_PS::Release()
{
	if(vs) {
		delete vs;
		vs = nullptr;
	}
	if(ps) {
		delete ps;
		ps = nullptr;
	}
	SAFE_RELEASE(pso);
}

HRESULT SimpleShader_VS_PS::InitializeInternal(LPCWSTR vs_path, LPCWSTR ps_path)
{
#if 0	// シェーダーソースを読み込みたい場合
	vs = new Dx12Shader(vs_path, "main","vs_5_0");
	if(!vs->IsValid()) {
		return Result(vs);
	}
	ps = new Dx12Shader(ps_path, "main","ps_5_0");
	if(!ps->IsValid()) {
		return Result(ps);
	}
#else
	vs = new Dx12Shader(vs_path);
	if(!vs->IsValid()) {
		return Result(vs);
	}
	ps = new Dx12Shader(ps_path);
	if(!ps->IsValid()) {
		return Result(ps);
	}
#endif
	return S_OK;
}

HRESULT SimpleShader_VS_PS::Result(Dx12Shader *p)
{
	std::string err = p->Error() + "\n";
	OutputDebugStringA(err.c_str());
	return p->State();
}

//////////////////////////////////////////////////////////////////////////////

SimpleResourceSet::SimpleResourceSet(D3D12_PRIMITIVE_TOPOLOGY prim) noexcept
	: primitiveTopology(prim)
	, pConstantBufferVS(nullptr)
	, pConstantBufferPS(nullptr)
	, pConstantBufferForDepthVS(nullptr)
	, pConstantBufferForShadowPS(nullptr)
	, pVertexBuffer(nullptr)
	, pIndexBuffer(nullptr)
	, pTextureBuffer(nullptr)
	, pTextureHeap(nullptr)
	, pSamplerHeap(nullptr)
	, handleSrv{}
	, handleSampler{}
	, vertexBufferView{}
	, indexBufferView{}
	, indexCount(0)
	, matrices{}
	, pixelLightingSet{}
	, world{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}
{
	matrices.modelview = DirectX::XMMatrixIdentity();
	matrices.projection = DirectX::XMMatrixIdentity();
	matrices.world = DirectX::XMMatrixIdentity();
	matrices.lightViewProj = DirectX::XMMatrixIdentity();
}

SimpleResourceSet::~SimpleResourceSet()
{
	Release();
}

void SimpleResourceSet::Release()
{
	SAFE_RELEASE(pConstantBufferVS);
	SAFE_RELEASE(pConstantBufferPS);
	SAFE_RELEASE(pConstantBufferForDepthVS);
	SAFE_RELEASE(pConstantBufferForShadowPS);
	SAFE_RELEASE(pVertexBuffer);
	SAFE_RELEASE(pIndexBuffer);
	SAFE_RELEASE(pTextureBuffer);
	SAFE_RELEASE(pTextureHeap);
	SAFE_RELEASE(pSamplerHeap);
}

HRESULT SimpleResourceSet::Allocate(Dx12Util *pDx12, const AllocateSet &allocateSet, UINT count)
{
	return Allocate(pDx12,
		static_cast<UINT>(allocateSet.vertexShaderConstantBufferSize),
		static_cast<UINT>(allocateSet.pixelShaderConstantBufferSize),
		static_cast<UINT>(allocateSet.vertexLayoutStrideSize * count),
		static_cast<UINT>(allocateSet.indexLayoutStrideSize * count));
}

HRESULT SimpleResourceSet::Allocate(Dx12Util *pDx12, UINT cbVs_size, UINT cbPs_size, UINT vertex_size, UINT index_size)
{
	HRESULT hr = S_OK;
	// 定数バッファ
	if(cbVs_size > 0) {
		hr = pDx12->GetBuffer(&pConstantBufferVS, cbVs_size);
		if(FAILED(hr)) {
			return hr;
		}
	}
	if(cbPs_size > 0) {
		hr = pDx12->GetBuffer(&pConstantBufferPS, cbPs_size);
		if(FAILED(hr)) {
			return hr;
		}
	}
	// 頂点バッファ
	if(vertex_size > 0) {
		hr = pDx12->GetBuffer(&pVertexBuffer, vertex_size);
		if(FAILED(hr)) {
			return hr;
		}
	}
	// インデクスバッファ
	if(index_size > 0) {
		hr = pDx12->GetBuffer(&pIndexBuffer, index_size);
		if(FAILED(hr)) {
			return hr;
		}
	}
	return hr;
}

HRESULT SimpleResourceSet::AllocateForDepth(Dx12Util *pDx12, UINT cbSize)
{
	HRESULT hr = S_OK;
	// 定数バッファ
	if(cbSize > 0) {
		return pDx12->GetBuffer(&pConstantBufferForDepthVS, cbSize);
	}
	return hr;
}

HRESULT SimpleResourceSet::AllocateForShadow(Dx12Util *pDx12, UINT cbSize)
{
	HRESULT hr = S_OK;
	// 定数バッファ
	if(cbSize > 0) {
		return pDx12->GetBuffer(&pConstantBufferForShadowPS, cbSize);
	}
	return hr;
}

void SimpleResourceSet::SetIndexCount(UINT index_count)
{
	indexCount = index_count;
}

UINT SimpleResourceSet::IndexCount() const
{
	return indexCount;
}

void SimpleResourceSet::UpdateMatrices(UINT width, UINT height, SimpleCamera &camera)
{
	camera.GetProjectionMatrixLH(width, height, matrices.projection);
	camera.GetLookAtMatrixLH(matrices.modelview);
	Dx12Util::Transpose(matrices.world, world);
}

void SimpleResourceSet::UpdateMatrices(ShadowMapDesc *shadowMapDesc, SimpleCamera &light)
{
	DirectX::XMMATRIX projection, modelview;
	ShadowMapUtil shadow(shadowMapDesc);
	light.GetProjectionMatrixLH(shadow.Size(), shadow.Size(), projection);
	light.GetLookAtMatrixLH(modelview);
	matrices.lightViewProj = XMMatrixMultiply(projection, modelview);	// hlsl 注入でちょっとややこしい
}

void SimpleResourceSet::SetupInputAssembler(ID3D12GraphicsCommandList *pCommandList) const
{
	pCommandList->IASetPrimitiveTopology(primitiveTopology);
	pCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	pCommandList->IASetIndexBuffer(&indexBufferView);
}
