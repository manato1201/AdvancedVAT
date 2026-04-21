#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include "types.h"

class Dx12Util;
class SimpleCamera;
struct ShadowMapDesc;


namespace SimpleConstantBuffer {

// projection と modelview は分ける必要ないけど学習の理解度補助の為に分けてる
struct ProjectionModelviewWorld {
	DirectX::XMMATRIX	projection;
	DirectX::XMMATRIX	modelview;
	DirectX::XMMATRIX	world;
	DirectX::XMMATRIX	lightViewProj;
};

// 全て合成して１つに纏めたバージョン
struct WorldMVP {
	DirectX::XMMATRIX	worldMVP;
};

struct PixelColor {
	DirectX::XMVECTOR	color;
};

struct PixelLightingSet {
	DirectX::XMVECTOR	lightPos;
	DirectX::XMVECTOR	diffuseColor;
	DirectX::XMVECTOR	cameraPos;
	// diffuseColor[3] など余った変数に押し込んでも良いが、今は判りやすさ優先
	float				intensity;
	float				power;
};

struct PixelPostParams {
	DirectX::XMVECTOR	params;
};

struct PixelShadowParams {
	DirectX::XMVECTOR	bias;
};

} // SimpleConstantBuffer


struct AllocateSet {
	size_t	vertexShaderConstantBufferSize;
	size_t	pixelShaderConstantBufferSize;
	size_t	vertexLayoutStrideSize;
	size_t	indexLayoutStrideSize;
};


// シンプルな構成でモデルのリソースを管理する
class SimpleResourceSet {
public:
	SimpleResourceSet(D3D12_PRIMITIVE_TOPOLOGY prim) noexcept;

	virtual ~SimpleResourceSet();

	void Release();

	HRESULT Allocate(Dx12Util *pDx12, UINT cbVs_size, UINT cbPs_size, UINT vertex_size, UINT index_size);
	HRESULT Allocate(Dx12Util *pDx12, const AllocateSet &allocateSet, UINT count);
	HRESULT AllocateForDepth(Dx12Util *pDx12, UINT cbSize);
	HRESULT AllocateForShadow(Dx12Util *pDx12, UINT cbSize);

	void UpdateMatrices(UINT width, UINT height, SimpleCamera &camera);
	void UpdateMatrices(ShadowMapDesc *shadowMapDesc, SimpleCamera &light);

	// // Input Assembler (IA) は GPU パイプラインにデータを供給する最初のステージ
	void SetupInputAssembler(ID3D12GraphicsCommandList *pCommandList) const;

	void SetIndexCount(UINT index_count);
	UINT IndexCount() const;

public:
	// todo: 直接アクセスはなるべくやらないように (m_ サフィックスも付ける?)
	D3D12_PRIMITIVE_TOPOLOGY	primitiveTopology;
	ID3D12Resource				*pConstantBufferVS, *pConstantBufferPS;
	ID3D12Resource				*pConstantBufferForDepthVS;		// 同じメモリだと 256 バイト境界の処理が煩わしいのでリソースを分ける
	ID3D12Resource				*pConstantBufferForShadowPS;	// 同じメモリだと 256 バイト境界の処理が煩わしいのでリソースを分ける
	ID3D12Resource				*pVertexBuffer;
	ID3D12Resource				*pIndexBuffer;
	ID3D12Resource				*pTextureBuffer;
	ID3D12DescriptorHeap		*pTextureHeap;
	ID3D12DescriptorHeap		*pSamplerHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE	handleSrv;
	D3D12_CPU_DESCRIPTOR_HANDLE	handleSampler;
	D3D12_VERTEX_BUFFER_VIEW	vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW		indexBufferView;
private:
	UINT						indexCount;

public:
	SimpleConstantBuffer::ProjectionModelviewWorld	matrices;
	SimpleConstantBuffer::PixelLightingSet			pixelLightingSet;
	SimpleConstantBuffer::PixelShadowParams			shadowParams;
	float44											world;	// row-major によるユーザー更新用行列 (matrices.world へ転置コピーされる)
};
