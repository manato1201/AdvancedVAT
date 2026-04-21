#pragma once
#include <string>
#include <DirectXMath.h>

class SimpleShader_VS_PS;
class RootSignature;
class SimpleTexture;
struct ID3D12PipelineState;

namespace Scene {

bool AddShader(const std::string &name, SimpleShader_VS_PS *shader);
SimpleShader_VS_PS *GetShader(const std::string &name);

bool AddSignature(const std::string &name, RootSignature *signature);
RootSignature *GetSignature(const std::string &name);

bool AddShader(const std::string &name, SimpleShader_VS_PS *shader);
SimpleShader_VS_PS *GetShader(const std::string &name);

bool AddPso(const std::string &name, ID3D12PipelineState *pso);
ID3D12PipelineState *GetPso(const std::string &name);

bool AddPsoDepth(const std::string &name, ID3D12PipelineState *pso);
ID3D12PipelineState *GetPsoDepth(const std::string &name);

bool AddTexture(const std::string &name, SimpleTexture *texture);
SimpleTexture *GetTexture(const std::string &name);

void ReleaseResources();

///// シーン独自のコンスタントバッファ /////
namespace ConstantBuffer {

// 64KB に収まる範囲で拡張可能
struct Float3Array100 {
	DirectX::XMMATRIX	projection;
	DirectX::XMVECTOR	point[100];
};

struct PixelColor {
	DirectX::XMVECTOR	color;
};

} // ConstantBuffer

const UINT	VAT_meshSize = 32;

} // Scene
