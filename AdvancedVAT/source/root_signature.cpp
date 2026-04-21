#include "stdafx.h"
#include "root_signature.h"
#include "shader.h"
#include "dx12_util.h"

namespace {

std::wstring UTF8ToWideChar(const std::string &str)
{
	if(str.empty()) return {};

	int size = MultiByteToWideChar(
		CP_UTF8,		// UTF-8
		0,				// フラグ（エラーチェックしたいなら MB_ERR_INVALID_CHARS）
		str.c_str(),	// 入力
		-1,				// null 終端まで
		nullptr, 0		// 必要サイズ取得
	);

	std::wstring result(size > 0 ? size - 1 : 0, L'\0'); // 終端ぶんを引く
	if(size > 1) {
		MultiByteToWideChar(
			CP_UTF8,
			0,
			str.c_str(),
			-1,
			&result[0],
			size
		);
	}

	return result;
}

} // unnamed


RootSignature::RootSignature() noexcept
	: m_signature(nullptr)
	, m_result(E_FAIL)
{
}

RootSignature::~RootSignature()
{
	SAFE_RELEASE(m_signature);
}

void RootSignature::SetName(const std::string &name)
{
	if(m_signature) {
		m_signature->SetName(UTF8ToWideChar(name).c_str());
	}
}

ID3D12PipelineState *RootSignature::CreateDefaultPipelineState(
	Dx12Util *pDx12,
	SimpleShader_VS_PS *shaderSet,
	const D3D12_INPUT_LAYOUT_DESC &layout,
	PrimtiveType primitiveType,
	BlendType blendType, DepthUse depth) const
{
	ID3D12PipelineState *pso = nullptr;

	// Graphics Pipeline State
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_signature;
	// input layout
	psoDesc.InputLayout = layout;
	// shader
	psoDesc.VS = shaderSet->VS()->ByteCode();
	psoDesc.PS = shaderSet->PS()->ByteCode();
	// sample
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	// render target
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = pDx12->RtvFormat();
	// primitive (D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE とか)
	psoDesc.PrimitiveTopologyType = static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(primitiveType);
	// depth stencil
	// 現状 depth func は固定
	psoDesc.DSVFormat = pDx12->DsvFormat();
	pDx12->SetDefaultDepthStencilState(psoDesc.DepthStencilState, depth != DepthUse::None);
	// todo: 半透明のパスで書き込みのみ無効化する場合は
	// DepthEnable = true, DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO とする

	// rasterizer
	D3D12_RASTERIZER_DESC ras;
	pDx12->SetDefaultRasterizer(ras);
	psoDesc.RasterizerState = ras;

	D3D12_RENDER_TARGET_BLEND_DESC rtBlend {};
	if(blendType == BlendType::None) {
		rtBlend = D3D12_RENDER_TARGET_BLEND_DESC {
			FALSE, FALSE,					// 両方 true は出来ない
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL
		};
	} else if(blendType == BlendType::ONE_MINUS_SRC_ALPHA) {
		rtBlend = D3D12_RENDER_TARGET_BLEND_DESC {
			TRUE,                          // BlendEnable
			FALSE,                         // LogicOpEnable
			D3D12_BLEND_SRC_ALPHA,         // SrcBlend
			D3D12_BLEND_INV_SRC_ALPHA,     // DestBlend
			D3D12_BLEND_OP_ADD,            // BlendOp
			D3D12_BLEND_ONE,               // SrcBlendAlpha
			D3D12_BLEND_ZERO,              // DestBlendAlpha
			D3D12_BLEND_OP_ADD,            // BlendOpAlpha
			D3D12_LOGIC_OP_NOOP,           // LogicOp
			D3D12_COLOR_WRITE_ENABLE_ALL   // RenderTargetWriteMask
		};
	} else if(blendType == BlendType::COMPOSITE) {
		// OMSetBlendFactor({0.5f, 0.5f, 0.5f, 0.5f}) など必要
		rtBlend = D3D12_RENDER_TARGET_BLEND_DESC {
			TRUE,                          // BlendEnable
			FALSE,                         // LogicOpEnable
			D3D12_BLEND_BLEND_FACTOR,      // SrcBlend        = F
			D3D12_BLEND_INV_BLEND_FACTOR,  // DestBlend       = 1 - F
			D3D12_BLEND_OP_ADD,            // BlendOp
			D3D12_BLEND_ZERO,              // SrcBlendAlpha
			D3D12_BLEND_ONE,               // DestBlendAlpha
			D3D12_BLEND_OP_ADD,            // BlendOpAlpha
			D3D12_LOGIC_OP_NOOP,           // LogicOp
			D3D12_COLOR_WRITE_ENABLE_ALL   // RenderTargetWriteMask
		};
	} else if (blendType == BlendType::ADD) {
		// Cdst += Csrc (αも)
		rtBlend = D3D12_RENDER_TARGET_BLEND_DESC{
			TRUE,                          // BlendEnable
			FALSE,                         // LogicOpEnable
			D3D12_BLEND_ONE,               // SrcBlend
			D3D12_BLEND_ONE,               // DestBlend
			D3D12_BLEND_OP_ADD,            // BlendOp
			D3D12_BLEND_ONE,               // SrcBlendAlpha
			D3D12_BLEND_ONE,               // DestBlendAlpha
			D3D12_BLEND_OP_ADD,            // BlendOpAlpha
			D3D12_LOGIC_OP_NOOP,           // LogicOp
			D3D12_COLOR_WRITE_ENABLE_ALL
		};
		// 元のαを保持したい場合
		// rtBlend.SrcBlendAlpha  = D3D12_BLEND_ZERO; // αは書き換えず
		// rtBlend.DestBlendAlpha = D3D12_BLEND_ONE;
		// rtBlend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
	}

	// 全ての RT に同じ rtBlend をセット (MRT はその時考える)
	pDx12->SetBlendState(psoDesc.BlendState, rtBlend);

	auto hr = pDx12->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
	return SUCCEEDED(hr) ? pso : nullptr;
}


ID3D12PipelineState *RootSignature::CreateDefaultPipelineState2RT(
	Dx12Util *pDx12,
	SimpleShader_VS_PS *shaderSet,
	const D3D12_INPUT_LAYOUT_DESC &layout,
	PrimtiveType primitiveType,
	BlendType blendType, DepthUse depth) const
{
	ID3D12PipelineState *pso = nullptr;

	// Graphics Pipeline State
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_signature;
	// input layout
	psoDesc.InputLayout = layout;
	// shader
	psoDesc.VS = shaderSet->VS()->ByteCode();
	psoDesc.PS = shaderSet->PS()->ByteCode();
	// sample
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	// render target
	psoDesc.NumRenderTargets = 2;
	psoDesc.RTVFormats[0] = pDx12->RtvFormat();
	psoDesc.RTVFormats[1] = pDx12->RtvFormat();
	// primitive (D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE とか)
	psoDesc.PrimitiveTopologyType = static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(primitiveType);
	// depth stencil
	// 現状 depth func は固定
	psoDesc.DSVFormat = pDx12->DsvFormat();
	pDx12->SetDefaultDepthStencilState(psoDesc.DepthStencilState, depth != DepthUse::None);
	// todo: 半透明のパスで書き込みのみ無効化する場合は
	// DepthEnable = true, DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO とする

	// rasterizer
	D3D12_RASTERIZER_DESC ras;
	pDx12->SetDefaultRasterizer(ras);
	psoDesc.RasterizerState = ras;

	D3D12_RENDER_TARGET_BLEND_DESC rtBlend {};
	if(blendType == BlendType::None) {
		rtBlend = D3D12_RENDER_TARGET_BLEND_DESC {
			FALSE, FALSE,					// 両方 true は出来ない
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL
		};
	} else if(blendType == BlendType::ONE_MINUS_SRC_ALPHA) {
		rtBlend = D3D12_RENDER_TARGET_BLEND_DESC {
			TRUE,                          // BlendEnable
			FALSE,                         // LogicOpEnable
			D3D12_BLEND_SRC_ALPHA,         // SrcBlend
			D3D12_BLEND_INV_SRC_ALPHA,     // DestBlend
			D3D12_BLEND_OP_ADD,            // BlendOp
			D3D12_BLEND_ONE,               // SrcBlendAlpha
			D3D12_BLEND_ZERO,              // DestBlendAlpha
			D3D12_BLEND_OP_ADD,            // BlendOpAlpha
			D3D12_LOGIC_OP_NOOP,           // LogicOp
			D3D12_COLOR_WRITE_ENABLE_ALL   // RenderTargetWriteMask
		};
	} else if(blendType == BlendType::COMPOSITE) {
		// OMSetBlendFactor({0.5f, 0.5f, 0.5f, 0.5f}) など必要
		rtBlend = D3D12_RENDER_TARGET_BLEND_DESC {
			TRUE,                          // BlendEnable
			FALSE,                         // LogicOpEnable
			D3D12_BLEND_BLEND_FACTOR,      // SrcBlend        = F
			D3D12_BLEND_INV_BLEND_FACTOR,  // DestBlend       = 1 - F
			D3D12_BLEND_OP_ADD,            // BlendOp
			D3D12_BLEND_ZERO,              // SrcBlendAlpha
			D3D12_BLEND_ONE,               // DestBlendAlpha
			D3D12_BLEND_OP_ADD,            // BlendOpAlpha
			D3D12_LOGIC_OP_NOOP,           // LogicOp
			D3D12_COLOR_WRITE_ENABLE_ALL   // RenderTargetWriteMask
		};
	}

	// 全ての RT に同じ rtBlend をセット (MRT はその時考える)
	pDx12->SetBlendState(psoDesc.BlendState, rtBlend);

	auto hr = pDx12->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
	return SUCCEEDED(hr) ? pso : nullptr;
}


ID3D12PipelineState *RootSignature::CreateDepthPipelineState(
	Dx12Util *pDx12,
	SimpleShader_VS_PS *shaderSet,
	const D3D12_INPUT_LAYOUT_DESC &layout,
	PrimtiveType primitiveType, DepthUse depth) const
{
	ID3D12PipelineState *pso = nullptr;

	// Graphics Pipeline State
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_signature;
	// input layout
	psoDesc.InputLayout = layout;
	// shader
	psoDesc.VS = shaderSet->VS()->ByteCode();
	psoDesc.PS = { nullptr, 0 };	// !!
	// sample
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	// render target
	psoDesc.NumRenderTargets = 0;	// !!
	// primitive (D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE とか)
	psoDesc.PrimitiveTopologyType = static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(primitiveType);
	// depth stencil
	// 現状 depth func は固定
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pDx12->SetDefaultDepthStencilState(psoDesc.DepthStencilState, depth != DepthUse::None);

	// rasterizer
	D3D12_RASTERIZER_DESC ras;
	pDx12->SetDefaultRasterizer(ras);
	psoDesc.RasterizerState = ras;

	D3D12_RENDER_TARGET_BLEND_DESC rtBlend =
		D3D12_RENDER_TARGET_BLEND_DESC {
		FALSE, FALSE,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL
	};
	// 全ての RT に同じ rtBlend をセット (depth 描画で MRT はやらんよね)
	pDx12->SetBlendState(psoDesc.BlendState, rtBlend);

#if 0
	{
		psoDesc.DepthStencilState.DepthEnable    = TRUE;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
#ifdef REVERSED_Z
		psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL; // or GREATER
#else
		psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
#endif
		// ラスタも一旦安全側（描けることの確認用）
		psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.DepthBias             = 0;
		psoDesc.RasterizerState.SlopeScaledDepthBias  = 0.0f;

		// RTVなし/PSなし/DSV一致は既にOK
		psoDesc.NumRenderTargets = 0;
		psoDesc.PS = { nullptr, 0 };
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	}
#endif

	auto hr = pDx12->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
	return SUCCEEDED(hr) ? pso : nullptr;
}


RootSignature_CBV::RootSignature_CBV(ID3D12Device *pDevice)
	: RootSignature{}
{
	// ルートパラメータ
	D3D12_ROOT_PARAMETER param[1] = {};

	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
	sigDesc.NumParameters = _countof(param);
	sigDesc.pParameters = param;
	sigDesc.NumStaticSamplers = 0;
	sigDesc.pStaticSamplers = nullptr;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ID3DBlob *blob = nullptr, *error = nullptr;
	m_result = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if(SUCCEEDED(m_result)) {
		m_result = pDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_signature));
	}
}

RootSignature_CBV1P1::RootSignature_CBV1P1(ID3D12Device *pDevice)
	: RootSignature{}
{
	// ルートパラメータ
	D3D12_ROOT_PARAMETER param[2] = {};

	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	param[0].Descriptor.ShaderRegister = 0;	// b0
	param[0].Descriptor.RegisterSpace = 0;

	param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[1].Descriptor.ShaderRegister = 1;	// b1
	param[1].Descriptor.RegisterSpace = 0;

	D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
	sigDesc.NumParameters = _countof(param);
	sigDesc.pParameters = param;
	sigDesc.NumStaticSamplers = 0;
	sigDesc.pStaticSamplers = nullptr;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	ID3DBlob *blob = nullptr, *error = nullptr;
	m_result = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if(SUCCEEDED(m_result)) {
		m_result = pDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_signature));
	}
}

RootSignature_CBV_SRV_Sampler::RootSignature_CBV_SRV_Sampler(ID3D12Device *pDevice)
	: RootSignature{}
{
	// デスクリプタレンジ
	D3D12_DESCRIPTOR_RANGE range[2] = {};	// SRV, sampler
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	// for SRV (shader resource view)
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0;	// hlsl: register(t0)
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;	// for sampler
	range[1].NumDescriptors = 1;
	range[1].BaseShaderRegister = 0;	// hlsl: register(s0)
	range[1].RegisterSpace = 0;
	range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメータ
	D3D12_ROOT_PARAMETER param[3] = {};

	//param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;//D3D12_SHADER_VISIBILITY_VERTEX;	// 頂点シェーダーで参照するよ
	//param[0].DescriptorTable.NumDescriptorRanges = _countof(range);
	//param[0].DescriptorTable.pDescriptorRanges = range;
	param[0].Descriptor.ShaderRegister = 0;
	param[0].Descriptor.RegisterSpace = 0;

	param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param[1].DescriptorTable.NumDescriptorRanges = 1;
	param[1].DescriptorTable.pDescriptorRanges = &range[0];

	param[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param[2].DescriptorTable.NumDescriptorRanges = 1;
	param[2].DescriptorTable.pDescriptorRanges = &range[1];

	D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
	sigDesc.NumParameters = _countof(param);
	sigDesc.pParameters = param;
	sigDesc.NumStaticSamplers = 0;
	sigDesc.pStaticSamplers = nullptr;	// todo?: サンプラは静的サンプラにしておくとサンプラヒープをバインドしなくて済む
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
	ID3DBlob *blob = nullptr, *error = nullptr;
	m_result = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if(SUCCEEDED(m_result)) {
		m_result = pDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_signature));
	}
}

RootSignature_CBV1P1_SRV::RootSignature_CBV1P1_SRV(ID3D12Device *pDevice)
	: RootSignature{}
{
	// デスクリプタレンジ
	D3D12_DESCRIPTOR_RANGE range[1] = {};	// SRV
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	// for SRV (shader resource view)
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0;
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメータ
	D3D12_ROOT_PARAMETER param[3] = {};

	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	param[0].Descriptor.ShaderRegister = 0;	// b0
	param[0].Descriptor.RegisterSpace = 0;

	param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[1].Descriptor.ShaderRegister = 1;	// b1
	param[1].Descriptor.RegisterSpace = 0;

	param[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param[2].DescriptorTable.NumDescriptorRanges = 1;
	param[2].DescriptorTable.pDescriptorRanges = &range[0];	// SRV (t0)

	D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
	sigDesc.NumParameters = _countof(param);
	sigDesc.pParameters = param;
	sigDesc.NumStaticSamplers = 0;
	sigDesc.pStaticSamplers = nullptr;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
	ID3DBlob *blob = nullptr, *error = nullptr;
	m_result = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if(SUCCEEDED(m_result)) {
		m_result = pDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_signature));
	}
}

RootSignature_CBV1P1_SRV_Sampler::RootSignature_CBV1P1_SRV_Sampler(ID3D12Device *pDevice)
	: RootSignature{}
{
	// デスクリプタレンジ
	D3D12_DESCRIPTOR_RANGE range[2] = {};	// SRV, sampler
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	// for SRV (shader resource view)
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0;
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;	// for sampler
	range[1].NumDescriptors = 1;
	range[1].BaseShaderRegister = 0;
	range[1].RegisterSpace = 0;
	range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメータ
	D3D12_ROOT_PARAMETER param[4] = {};

	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	param[0].Descriptor.ShaderRegister = 0;	// b0
	param[0].Descriptor.RegisterSpace = 0;

	param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[1].Descriptor.ShaderRegister = 1;	// b1
	param[1].Descriptor.RegisterSpace = 0;

	param[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param[2].DescriptorTable.NumDescriptorRanges = 1;
	param[2].DescriptorTable.pDescriptorRanges = &range[0];	// SRV (t0)

	param[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param[3].DescriptorTable.NumDescriptorRanges = 1;
	param[3].DescriptorTable.pDescriptorRanges = &range[1];	// Sampler (s0)

	D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
	sigDesc.NumParameters = _countof(param);
	sigDesc.pParameters = param;
	sigDesc.NumStaticSamplers = 0;
	sigDesc.pStaticSamplers = nullptr;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
	ID3DBlob *blob = nullptr, *error = nullptr;
	m_result = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if(SUCCEEDED(m_result)) {
		m_result = pDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_signature));
	}
}

RootSignature_CBV1P2_SRV_Sampler::RootSignature_CBV1P2_SRV_Sampler(ID3D12Device *pDevice)
	: RootSignature{}
{
	// デスクリプタレンジ
	D3D12_DESCRIPTOR_RANGE range[2] = {};	// SRV, sampler
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;	// for SRV (shader resource view)
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0;
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;	// for sampler
	range[1].NumDescriptors = 1;
	range[1].BaseShaderRegister = 0;
	range[1].RegisterSpace = 0;
	range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// ルートパラメータ
	D3D12_ROOT_PARAMETER param[5] = {};

	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	param[0].Descriptor.ShaderRegister = 0;	// b0
	param[0].Descriptor.RegisterSpace = 0;

	param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[1].Descriptor.ShaderRegister = 1;	// b1
	param[1].Descriptor.RegisterSpace = 0;

	param[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[2].Descriptor.ShaderRegister = 2;	// b2
	param[2].Descriptor.RegisterSpace = 0;

	param[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param[3].DescriptorTable.NumDescriptorRanges = 1;
	param[3].DescriptorTable.pDescriptorRanges = &range[0];	// SRV (t0)

	param[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param[4].DescriptorTable.NumDescriptorRanges = 1;
	param[4].DescriptorTable.pDescriptorRanges = &range[1];	// Sampler (s0)

	D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
	sigDesc.NumParameters = _countof(param);
	sigDesc.pParameters = param;
	sigDesc.NumStaticSamplers = 0;
	sigDesc.pStaticSamplers = nullptr;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
	//| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
	ID3DBlob *blob = nullptr, *error = nullptr;
	m_result = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if(SUCCEEDED(m_result)) {
		m_result = pDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_signature));
	}
}

RootSignature_Depth::RootSignature_Depth(ID3D12Device *pDevice)
	: RootSignature{}
{
	// ルートパラメータ
	D3D12_ROOT_PARAMETER param[1] = {};

	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_DESC sigDesc = {};
	sigDesc.NumParameters = _countof(param);
	sigDesc.pParameters = param;
	sigDesc.NumStaticSamplers = 0;
	sigDesc.pStaticSamplers = nullptr;
	sigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
		| D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	ID3DBlob *blob = nullptr, *error = nullptr;
	m_result = D3D12SerializeRootSignature(&sigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if(SUCCEEDED(m_result)) {
		m_result = pDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_signature));
	}
}
