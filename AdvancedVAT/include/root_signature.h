#pragma once
#include <d3d12.h>
#include <string>
#include "types.h"

class Dx12Util;
class SimpleShader_VS_PS;

enum class PrimtiveType
{
	Point = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,
	Line = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
	Triangle = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
};

enum class BlendType
{
	None = 0,
	ONE_MINUS_SRC_ALPHA,
	COMPOSITE,
	ADD,
	// todo: 必要に応じて増やす
};

enum class DepthUse
{
	None = 0,
	ReadWrite,
	// todo: 必要に応じて増やす
};

// ルートシグネチャはそのパイプラインでどのようなデータをどのようにセットするかを記述したもの
//
// D3D12 のパイプラインにリソース（CBV/SRV/UAV/サンプラなど）を結びつけるためには
// どのスロットにどの種類のリソースをバインドするかを GPU に知らせる必要がある
// その「バインディングの仕様書」が Root Signature（ルートシグネチャ）
//
// ・パイプラインでどんな種類のリソースをどう受け取るかのレイアウト宣言
// ・PipelineStateObject と密接に結びつく
// ・シェーダーに外部から供給されるデータ (バッファ, テクスチャ等) の仕様書
// ・cbuffer PerFrame : register(b0) と書いたときに b0 にはどの CBV が来るかも GPU に伝える
// ・アプリ側のリソース -> シェーダのレジスタへのマッピングを定義
class RootSignature
{
public:
	RootSignature() noexcept;
	virtual ~RootSignature();

	ID3D12RootSignature *Signature() const { return m_signature; }
	HRESULT Result() const { return m_result; }

	void SetName(const std::string &name);

	// 教材向けにある程度決め打ちで pipeline state を設定、depth func もここで固定
	ID3D12PipelineState *CreateDefaultPipelineState(
		Dx12Util *pDx12,
		SimpleShader_VS_PS *shaderSet,
		const D3D12_INPUT_LAYOUT_DESC &layout,
		PrimtiveType primitiveType,
		BlendType blendType, DepthUse depth) const;
	// RenderTarget を２つにする版
	ID3D12PipelineState *CreateDefaultPipelineState2RT(
		Dx12Util *pDx12,
		SimpleShader_VS_PS *shaderSet,
		const D3D12_INPUT_LAYOUT_DESC &layout,
		PrimtiveType primitiveType,
		BlendType blendType, DepthUse depth) const;

	ID3D12PipelineState *CreateDepthPipelineState(
		Dx12Util *pDx12,
		SimpleShader_VS_PS *shaderSet,
		const D3D12_INPUT_LAYOUT_DESC &layout,
		PrimtiveType primitiveType, DepthUse depth) const;

protected:
	ID3D12RootSignature	*m_signature;
	HRESULT				m_result;
};

///// RootSignature の生成は目的別に派生クラスで各々担当させる /////
// ヘタに条件式で管理するとあっという間に煩雑になるけどこれはこれで既に煩雑だな..

// ConstantBufferView(VS)
class RootSignature_CBV : public RootSignature
{
public:
	RootSignature_CBV(ID3D12Device *pDevice);
	~RootSignature_CBV() = default;
};

// ConstantBufferView(VS/PS)
class RootSignature_CBV1P1 : public RootSignature
{
public:
	RootSignature_CBV1P1(ID3D12Device *pDevice);
	~RootSignature_CBV1P1() = default;
};

// ConstantBufferView(VS), ShaderResourceView, Sampler
class RootSignature_CBV_SRV_Sampler : public RootSignature
{
public:
	RootSignature_CBV_SRV_Sampler(ID3D12Device *pDevice);
	~RootSignature_CBV_SRV_Sampler() = default;
};

// ConstantBufferView(VS/PS), ShaderResourceView
class RootSignature_CBV1P1_SRV : public RootSignature
{
public:
	RootSignature_CBV1P1_SRV(ID3D12Device *pDevice);
	~RootSignature_CBV1P1_SRV() = default;
};

// ConstantBufferView(VS/PS), ShaderResourceView, Sampler
class RootSignature_CBV1P1_SRV_Sampler : public RootSignature
{
public:
	RootSignature_CBV1P1_SRV_Sampler(ID3D12Device *pDevice);
	~RootSignature_CBV1P1_SRV_Sampler() = default;
};

// ConstantBufferView(VS/PS), ShaderResourceView, Sampler
class RootSignature_CBV1P2_SRV_Sampler : public RootSignature
{
public:
	RootSignature_CBV1P2_SRV_Sampler(ID3D12Device *pDevice);
	~RootSignature_CBV1P2_SRV_Sampler() = default;
};

// for depth write
class RootSignature_Depth : public RootSignature
{
public:
	RootSignature_Depth(ID3D12Device *pDevice);
	~RootSignature_Depth() = default;
};
