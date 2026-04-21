#pragma once
#include "dx12_util.h"
#include <cstdint>
#include <vector>
#include <string>

struct Vertex
{
	DirectX::XMFLOAT3 Position;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 UV;
	DirectX::XMFLOAT3 Tangent;
	DirectX::XMFLOAT4 Color;
};

struct Mesh {
	std::vector<Vertex>		Vertices;
	std::vector<uint32_t>	Indices;
	//std::wstring			DiffuseMap;
};

struct ImportDesc {
	std::string			filename;
	std::vector<Mesh>	meshes;
	std::string			error;
	bool inverseU = false;
	bool inverseV = false;
};

namespace AssImport {
extern bool LoadFbx(ImportDesc &desc);
}
