#include "stdafx.h"
#include "dx12_util.h"
#include <DirectXTex.h> // nuget package
#include <stdexcept>
#include "texture.h"

ID3D12Resource *SimpleTexture::LoadTextureFromFile(ID3D12Device *device, const std::wstring &filePath, size_t &oWidth, size_t &oHeihgt)
{
	ID3D12Resource *texture = nullptr;

	DirectX::ScratchImage image;
	HRESULT hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);
	if(FAILED(hr)) {
		throw std::runtime_error("Failed to load texture file.");
	}

	if(image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM) {
		DirectX::ScratchImage converted;
		hr = DirectX::Convert(
			image.GetImages(),			// 画像配列
			image.GetImageCount(),		// 画像枚数
			image.GetMetadata(),		// メタデータ
			DXGI_FORMAT_R8G8B8A8_UNORM,	// 変換したい形式
			DirectX::TEX_FILTER_DEFAULT,
			0,
			converted
		);
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to convert image format.");
		}
		image = std::move(converted);
	}

	const DirectX::TexMetadata& metadata = image.GetMetadata();

	D3D12_HEAP_PROPERTIES heapProp = {};
	heapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
	heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	heapProp.CreationNodeMask = 0;
	heapProp.VisibleNodeMask = 0;

	oWidth = metadata.width;
	oHeihgt = metadata.height;

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Width = static_cast<UINT>(metadata.width);
	resDesc.Height = static_cast<UINT>(metadata.height);
	resDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
	resDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	//resDesc.Format = metadata.format;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resDesc.SampleDesc.Quality = 0;
	resDesc.SampleDesc.Count = 1;

	hr = device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texture));
	if(FAILED(hr)) {
		throw std::runtime_error("Failed to create texture resource.");
	}

	// ピクセルデータを取得
	const DirectX::Image *img = image.GetImage(0, 0, 0); // 最初の MIP レベル
	if(!img) {
		texture->Release();
		throw std::runtime_error("Failed to retrieve image data.");
	}

	// 生のピクセルデータへのポインタ
	const uint8_t *pixelData = img->pixels;
	size_t rowPitch = img->rowPitch;
	size_t slicePitch = img->slicePitch;

#if 0
	std::vector<uint8_t> rgbaPixels;
	if(metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
		assert(metadata.format == DXGI_FORMAT_B8G8R8A8_UNORM);
		// ピクセルの並び変換
		rgbaPixels.resize(rowPitch * img->height);
		for(size_t  y= 0; y < img->height; y++) {
			const uint8_t *src = pixelData + y * rowPitch;
			uint8_t *dst = rgbaPixels.data() + y * rowPitch;
			for(size_t x = 0; x < img->width; x++) {
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[1];
				dst[3] = src[3];
				src += 4;
				dst += 4;
			}
		}
		pixelData = rgbaPixels.data();
	}
#endif

	D3D12_BOX box = {0, 0, 0, static_cast<UINT>(resDesc.Width), static_cast<UINT>(resDesc.Height), 1};
	hr = texture->WriteToSubresource(0, &box, pixelData, static_cast<UINT>(rowPitch), static_cast<UINT>(slicePitch));
	if(FAILED(hr)) {
		texture->Release();
		throw std::runtime_error("Failed to write texture resource.");
	}

	return texture;
}


SimpleTexture::SimpleTexture() noexcept
	: resource(nullptr)
	, upload(nullptr)
	, width{}
	, height{}
	, format(DXGI_FORMAT_R8G8B8A8_UNORM)
	, result(E_FAIL)
	, rowCount{}
	, uploadSize{}
	, state(D3D12_RESOURCE_STATE_COMMON)
	, footprint{}
{
}

SimpleTexture::SimpleTexture(ID3D12Device *pDevice, const std::wstring &filePath)
    : resource(nullptr)
	, upload(nullptr)
    , width{}
    , height{}
	, format(DXGI_FORMAT_R8G8B8A8_UNORM)
	, result(E_FAIL)
	, rowCount{}
	, uploadSize{}
	, state(D3D12_RESOURCE_STATE_COMMON)
	, footprint{}
{
    try {
        resource = LoadTextureFromFile(pDevice, filePath.c_str(), width, height);
    } catch(std::exception e) {
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
    }
}

SimpleTexture::SimpleTexture(ID3D12Device *pDevice, const std::string &filePath)
	: resource(nullptr)
	, upload(nullptr)
	, width{}
	, height{}
	, format(DXGI_FORMAT_R8G8B8A8_UNORM)
	, result(E_FAIL)
	, rowCount{}
	, uploadSize{}
	, state(D3D12_RESOURCE_STATE_COMMON)
	, footprint{}
{
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &wstr[0], size_needed);
	wstr.pop_back();	// 終端 null を除去

	try {
		resource = LoadTextureFromFile(pDevice, wstr.c_str(), width, height);
	} catch(std::exception e) {
		OutputDebugStringA(e.what());
		OutputDebugStringA("\n");
	}
}

SimpleTexture::~SimpleTexture()
{
	SAFE_RELEASE(resource);
	SAFE_RELEASE(upload);
}


bool SimpleTexture::LoadTextureFromMemory(ID3D12Device *pDevice, std::vector<float4> &data, UINT width, UINT height)
{
	format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	D3D12_RESOURCE_DESC texDesc{};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags  = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES hpDefault{};
	hpDefault.Type = D3D12_HEAP_TYPE_DEFAULT;

	HRESULT hr = pDevice->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
								D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));

	if(FAILED(hr)) return false;

	pDevice->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &rowCount, nullptr, &uploadSize);

	D3D12_HEAP_PROPERTIES hpUpload{};
	hpUpload.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC bufDesc{};
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Width = uploadSize;
	bufDesc.Height = 1;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.MipLevels = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.SampleDesc.Count = 1;

	hr = pDevice->CreateCommittedResource(
		&hpUpload, D3D12_HEAP_FLAG_NONE, &bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));

	if(FAILED(hr)) return false;

	// CPU からアップロードバッファに書き込む
	void *dstBase = nullptr;
	if(FAILED(hr = upload->Map(0, nullptr, &dstBase))) return false;

	const UINT srcRowPitch = width * sizeof(float4);
	const UINT dstRowPitch = footprint.Footprint.RowPitch; // 256B aligned

	BYTE *dst = static_cast<BYTE *>(dstBase) + footprint.Offset;
	const BYTE *src = reinterpret_cast<const BYTE *>(data.data());

	for(UINT y = 0; y < height; y++) {
		memcpy(dst + y * dstRowPitch, src + y * srcRowPitch, srcRowPitch);
	}
	upload->Unmap(0, nullptr);

	state = D3D12_RESOURCE_STATE_COPY_DEST;
	return true;
}

bool SimpleTexture::UploadTexture(ID3D12GraphicsCommandList *list)
{
	if(state == D3D12_RESOURCE_STATE_COPY_DEST) {
		// upload -> resource へコピー
		D3D12_TEXTURE_COPY_LOCATION dst{};
		dst.pResource = resource;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION src{};
		src.pResource = upload;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = footprint;

		list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		// 読み状態へ 1回だけ (VS,PS 両用)
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource   = resource;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = state;
		b.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |	D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		state = b.Transition.StateAfter;

		list->ResourceBarrier(1, &b);
		return false;
	}

	if(upload) {
		// todo: upload を release する
		// hint: アップロード終了は fence 値で同期しないと検知出来ない
		//       fence 値は ExecuteCommandLists() の後に Signal() した時に確定

		//upload->Release();
		//upload = nullptr;
	}
	return true;
}
