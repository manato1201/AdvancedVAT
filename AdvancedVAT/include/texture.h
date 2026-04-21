#pragma once
#include <string>
#include <vector>
#include <d3d12.h>

class SimpleTexture
{
public:
	SimpleTexture() noexcept;
	SimpleTexture(ID3D12Device *pDevice, const std::wstring &filePath);
	SimpleTexture(ID3D12Device *pDevice, const std::string &filePath);

	virtual ~SimpleTexture();

	ID3D12Resource *Resource() const { return resource; }
	HRESULT Result() const { return result; }

	template<typename T> T Width() const { return static_cast<T>(width); }
	template<typename T> T Height() const { return static_cast<T>(height); }

	static ID3D12Resource *LoadTextureFromFile(ID3D12Device *device, const std::wstring &filePath, size_t &oWidth, size_t &oHeight);

	bool LoadTextureFromMemory(ID3D12Device *pDevice, std::vector<float4> &data, UINT width, UINT height);
	bool UploadTexture(ID3D12GraphicsCommandList *cmd);

	DXGI_FORMAT Format() const { return format; }

protected:
	ID3D12Resource	*resource;
	ID3D12Resource	*upload;
	size_t			width, height;
	DXGI_FORMAT		format;
	HRESULT			result;

	UINT								rowCount;
	UINT64								uploadSize;
	D3D12_RESOURCE_STATES				state;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT	footprint;
};
