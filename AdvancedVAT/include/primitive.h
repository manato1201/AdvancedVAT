#pragma once
#include "dx12_resource.h"

class Dx12Util;
class RootSignature;
class SimpleCamera;
struct ShadowMapDesc;


// 頂点レイアウトは必要に応じて何個か用意しておく
namespace SimpleLayout {

struct Vertex {
	DirectX::XMFLOAT3	vx;
};

inline const D3D12_INPUT_ELEMENT_DESC vertex[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

//

struct VertexNormal {
	DirectX::XMFLOAT3	vx;
	DirectX::XMFLOAT3	nv;
};

inline const D3D12_INPUT_ELEMENT_DESC vertexNormal[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

//

struct VertexTexcoord {
	DirectX::XMFLOAT3	vx;
	DirectX::XMFLOAT2	uv;
};

inline const D3D12_INPUT_ELEMENT_DESC vertexTexcoord[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

//

struct VertexNormalTexcoord {
	DirectX::XMFLOAT3	vx;
	DirectX::XMFLOAT3	nv;
	DirectX::XMFLOAT2	uv;
	DirectX::XMFLOAT3	tangent;
};

inline const D3D12_INPUT_ELEMENT_DESC vertexNormalTexcoord[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

// この教材でスキンをやるかどうかは未定 (Siv3D 向けに作った fbx loader 持ってくるのちょい面倒なの)

} // SimpleLayout


// 描画パターンを幾つか限定する事によりシンプルに扱えるようにする
enum class DrawFunc {
	CONSTANT,	// 単一色
	TEX,		// テクスチャカラーのみで描画する
	NV,			// 法線だけで描画する (デバッグ用途)
	LIT,		// ライティング
	LIT_TEX,	// テクスチャとライティング
	COPYBACK,	// copyback 及び post-effect
};


// リソースセットを１つのモデルとして扱うクラス
// enum class DrawFunc に応じて描画コマンドを拡張する
class SimpleObject : public SimpleResourceSet {
public:
	SimpleObject(D3D12_PRIMITIVE_TOPOLOGY topology, DrawFunc func, RootSignature *signature, ID3D12PipelineState *pso, ID3D12PipelineState *psoDepth = nullptr) noexcept;

	~SimpleObject() = default;

	void SetScale(const float3 &scale);

	void SetPosition(const float3 &pos);

	inline void SetCameraIgnore(bool ignore, bool butUseWorld = false) { m_ignoreCamera = ignore; m_useWorld = butUseWorld; }
	inline bool IsCameraIgnore() const { return m_ignoreCamera; }

	inline void SetShadow(bool cast, bool receive) { m_castShadow = cast; m_receiveShadow = receive; }
	inline bool IsCastShadow() const { return m_castShadow; }
	inline bool IsReceiveShadow() const { return m_receiveShadow && !!m_signatureShadow && !!m_psoShadow; }

	void AttachShadow(ID3D12RootSignature *signature, ID3D12PipelineState *pso);
	void SetShadowBias(float bias);

	inline void SetHide(bool hide) { m_hide = hide; }
	inline bool IsHide() const { return m_hide; }

	inline DrawFunc GetDrawFunc() const { return m_drawFunc; }

	void UpdateMatricesForConstantBuffer(UINT width, UINT height, SimpleCamera &camera);
	void SetConstantBufferVS(ShadowMapDesc *shadowMapDesc, SimpleCamera &light);

	void SetConstantBufferPS(const float3 &lightPos, const float3 &diffuseColor);

	void DrawCommand(ID3D12GraphicsCommandList *list);
	void DrawDepthCommand(ID3D12GraphicsCommandList *list);

	inline ID3D12Resource *GetConstantBufferVS() const { return pConstantBufferVS; }
	inline ID3D12Resource *GetConstantBufferPS() const { return pConstantBufferPS; }
	inline ID3D12Resource *GetConstantBufferDepthVS() const { return pConstantBufferForDepthVS; }
	inline ID3D12Resource *GetConstantBufferShadowPS() const { return pConstantBufferForShadowPS; }

private:
	const DrawFunc		m_drawFunc;
	ID3D12RootSignature	*m_signature, *m_signatureShadow;
	ID3D12PipelineState	*m_pso, *m_psoDepth, *m_psoShadow;
	bool				m_ignoreCamera, m_useWorld;
	bool				m_hide;
	bool				m_castShadow, m_receiveShadow;

public:
	static HRESULT CreateLineStrip(
		Dx12Util *pDx12,
		RootSignature *rootSignature,
		ID3D12PipelineState *pipelineState,
		ID3D12PipelineState *pipelineStateDepth,
		const AllocateSet &allocateSet,
		UINT numPoints,
		SimpleObject **result);

	static HRESULT CreatePlane(
		Dx12Util *pDx12,
		RootSignature *rootSignature,
		ID3D12PipelineState *pipelineState,
		ID3D12PipelineState *pipelineStateDepth,
		const AllocateSet &allocateSet,
		SimpleObject **result);

	template<class T> T *MapConstantBufferVS() const {
		void *mapped = nullptr;
		auto hr = pConstantBufferVS->Map(0, nullptr, &mapped);
		return SUCCEEDED(hr) ? reinterpret_cast<T *>(mapped) : nullptr;
	}

	template<class T> T *MapConstantBufferPS() const {
		void *mapped = nullptr;
		auto hr = pConstantBufferPS->Map(0, nullptr, &mapped);
		return SUCCEEDED(hr) ? reinterpret_cast<T *>(mapped) : nullptr;
	}

	template<class T> inline void UnmapVS(T *mapped) {
		if(mapped) {
			pConstantBufferVS->Unmap(0, nullptr);
		}
	}

	template<class T> inline void UnmapPS(T *mapped) {
		if(mapped) {
			pConstantBufferPS->Unmap(0, nullptr);
		}
	}
};
