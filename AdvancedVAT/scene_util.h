#pragma once
#include <d3d12.h>
#include <string>
#include "primitive.h"
#include "root_signature.h"

class Dx12Util;
class SimpleCamera;
class SimpleObject;
class RootSignature;
struct ShadowMapDesc;
struct ImportDesc;

#define	LINE_POINTS		100

namespace Scene {

bool InitDefaultPso(Dx12Util *pDx12, const std::string &name, const std::string &sig_name, const std::string &shader_name,
	const D3D12_INPUT_LAYOUT_DESC &layout, PrimtiveType primitiveType, BlendType blendType, DepthUse depth);

bool InitDefaultPso2RT(Dx12Util *pDx12, const std::string &name, const std::string &sig_name, const std::string &shader_name,
	const D3D12_INPUT_LAYOUT_DESC &layout, PrimtiveType primitiveType, BlendType blendType, DepthUse depth);

bool InitDepthPSO(Dx12Util *pDx12, const std::string &name, const D3D12_INPUT_LAYOUT_DESC &layout,
	PrimtiveType primitiveType = PrimtiveType::Triangle,
	DepthUse depth = DepthUse::ReadWrite);

void SetupCamera(SimpleCamera &camera, float x, float y, float z, float n, float f, float fovy,
	float atx = 0, float aty = 0, float atz = 0, float upx = 0, float upy = 1, float upz = 0);
DirectX::XMMATRIX OrthoScreen(UINT width, UINT height);
SimpleObject *LoadFBX(Dx12Util *pDx12, ImportDesc &desc, RootSignature *signature, ID3D12PipelineState *pso, ID3D12PipelineState *psoDepth, bool hasTexture, HRESULT &hr);
SimpleObject *CreateRectangleObject(Dx12Util *pDx12, RootSignature *signature, ID3D12PipelineState *pipelineState, HRESULT &hr);
HRESULT BindTextureResource(ID3D12Device *pDevice, SimpleObject *obj, ID3D12Resource *textureResource, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
HRESULT BindShadowMapResource(ID3D12Device *pDevice, SimpleObject *obj, ShadowMapDesc *shadowMapDesc);
void SetViewport(D3D12_VIEWPORT &viewport, D3D12_RECT &scissor, UINT width, UINT height, UINT x = 0, UINT y = 0);
void SetViewport(ID3D12GraphicsCommandList *cmd, UINT width, UINT height, UINT x = 0, UINT y = 0);

struct FourPoints {
	float2	a, b, c, d;
};

std::pair<float, float> Bezier(float t, const FourPoints &points);
std::pair<float, float> Cardinal(float t, const FourPoints &points);
std::pair<float, float> BSpline(float t, const FourPoints &points);
std::pair<float, float> Hermite(float t, const FourPoints &points);

}
