#include "stdafx.h"
#include "scene.h"
#include "dx12_util.h"
#include "fbx_load.h"
#include "camera.h"
#include "shader.h"
#include "texture.h"
#include "primitive.h"
#include "root_signature.h"
#include "shadow.h"
#include "offscreen.h"
#include "scene_util.h"
#include "scene_resources.h"
#include <map>

#ifdef	_DEBUG
# define	DEBUG_LAYER	true			// DX12 のデバッグレイヤーを有効にする
#else
# define	DEBUG_LAYER	false
#endif

// exe があるディレクトリを返す（末尾に \ を含む）
// → どのカレントディレクトリから起動しても .cso を確実に発見できる
static std::wstring GetShaderDir()
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring ws(path);
	size_t pos = ws.find_last_of(L"\\/");
	return (pos != std::wstring::npos) ? ws.substr(0, pos + 1) : L"";
}

#define	USE_BG_TEX	false

// camera initial position
#define	CAMERA_X	0
#define	CAMERA_Y	0
#define	CAMERA_Z	-1000
// light initial position
#define	LIGHT_X		-800
#define	LIGHT_Y		800
#define	LIGHT_Z		-1200

//#define	TEXTURE_FILE	"data/earth_normalmap_2048x1024.jpg"
#define	TEXTURE_FILE	"data/toon_green.png"
#define	TEXTURE_FILE2	"data/PathfinderMap_hires.jpg"
#define	BACKGROUNDTEXTURE_FILE	"data/bg.png"

namespace Scene {
	// scene_draw.cpp
	void App(Dx12Util* pDx12, float durationSec, std::map<std::string, SimpleObject*>& objects, SimpleCamera& camera, SimpleCamera& light);
	// VAT_demo.cpp
	std::pair<SimpleObject*, SimpleTexture*> CreatePlaneWithVAT(Dx12Util* pDx12, RootSignature* sig, ID3D12PipelineState* pso, UINT N, float scale);
	// mic VAT control (VAT_demo.cpp)
	void InitVATAudio();
	void ShutdownVATAudio();
	void UpdateVATAudio();
	float GetVATAmp();
	float GetVATSpeed();
	float GetVATHz();
	float GetVATRms();
}

namespace {	// このプロジェクト固有のデータなどを定義するよ

	// fbx の定義
	struct FBX_DESC {
		std::string	name;			// 名前
		std::string	path;			// ファイルパス
		std::string sig_pso;		// pipeline state object の名前
		std::string	diffuseMap;		// (optional) diffuse texture
		float3	scale, position;
	};
	

	//VAT
	const std::vector<FBX_DESC> g_initialFbx = {
	};

	

	Dx12Util* g_pDx12 = nullptr;

	SimpleCamera	g_camera;
	SimpleCamera	g_light;	// shadow map で使うので camera と同じ扱いにする

	std::map<std::string, SimpleObject*>	g_objects;	// 描画オブジェクトを保持

	//////////////////////////////////////////////////////////////////////////////

	bool InitShader_VS_PS(const std::string& name, LPCWSTR vs, LPCWSTR ps)
	{
		return Scene::AddShader(name, SimpleShader_VS_PS::Initialize(vs, ps));
	}

	bool InitShaders()
	{
		auto dir = GetShaderDir();
		if (!InitShader_VS_PS("lighting",
			(dir + L"lighting_vs.cso").c_str(),
			(dir + L"lighting_ps.cso").c_str())) return false;

		if (!InitShader_VS_PS("vat",
			(dir + L"vat_vs.cso").c_str(),
			(dir + L"lighting_ps.cso").c_str())) return false;

		return true;
	}

	//////////////////////////////////////////////////////////////////////////////

	template<class T> HRESULT InitRootSignature(ID3D12Device* pDevice, const std::string& name)
	{
		auto signature = new T(pDevice);
		if (FAILED(signature->Result())) {
			HRESULT hr = signature->Result();
			delete signature;
			return hr;
		}

		if (Scene::AddSignature(name, signature)) {
			signature->SetName(name);
			return S_OK;
		}
		return E_FAIL;
	}

	bool InitDefaultPso(const std::string& name, const std::string& sig_name, const std::string& shader_name,
		const D3D12_INPUT_LAYOUT_DESC& layout, PrimtiveType primitiveType, BlendType blendType, DepthUse depth)
	{
		return Scene::InitDefaultPso(g_pDx12, name, sig_name, shader_name, layout, primitiveType, blendType, depth);
	}

	bool InitDefaultPso(const std::string& name, const D3D12_INPUT_LAYOUT_DESC& layout,
		PrimtiveType primitiveType = PrimtiveType::Triangle,
		BlendType blendType = BlendType::None,
		DepthUse depth = DepthUse::ReadWrite)
	{
		return Scene::InitDefaultPso(g_pDx12, name, name, name, layout, primitiveType, blendType, depth);
	}

	bool InitDefaultPso2RT(const std::string& name, const D3D12_INPUT_LAYOUT_DESC& layout,
		PrimtiveType primitiveType = PrimtiveType::Triangle,
		BlendType blendType = BlendType::None,
		DepthUse depth = DepthUse::ReadWrite)
	{
		return Scene::InitDefaultPso2RT(g_pDx12, name, name, name, layout, primitiveType, blendType, depth);
	}

	bool InitDepthPSO(const std::string& name, const D3D12_INPUT_LAYOUT_DESC& layout,
		PrimtiveType primitiveType = PrimtiveType::Triangle,
		DepthUse depth = DepthUse::ReadWrite)
	{
		return Scene::InitDepthPSO(g_pDx12, name, layout, primitiveType, depth);
	}

	HRESULT SetupSceneEnvironment(Dx12Util* pDx12)
	{
		if (!InitShaders()) return E_FAIL;

		HRESULT hr = S_OK;
		auto pDevice = pDx12->Device();

		bool shadow = SUCCEEDED(pDx12->CreateShadowMap(1024));

		// ルートシグネチャ
		
		if (FAILED(hr = InitRootSignature<RootSignature_CBV1P1_SRV_Sampler>(pDevice, "lighting"))) return hr;
		
		if (FAILED(hr = InitRootSignature<RootSignature_CBV1P1_SRV_Sampler>(pDevice, "copyback"))) return hr;
		

		// 頂点レイアウト
		D3D12_INPUT_LAYOUT_DESC layoutVertex = { SimpleLayout::vertex, _countof(SimpleLayout::vertex) };
		D3D12_INPUT_LAYOUT_DESC layoutVertexTexcoord = { SimpleLayout::vertexTexcoord, _countof(SimpleLayout::vertexTexcoord) };
		D3D12_INPUT_LAYOUT_DESC layoutVertexNormal = { SimpleLayout::vertexNormal, _countof(SimpleLayout::vertexNormal) };
		D3D12_INPUT_LAYOUT_DESC layoutVertexNormalTexcoord = { SimpleLayout::vertexNormalTexcoord, _countof(SimpleLayout::vertexNormalTexcoord) };

		// パイプラインステート
		/*
		* InitDefaultPso() はある程度定型パターンの Pipiline State Object を作るユーティリティ。
		* 最初に PSO に付ける名前、そして signature-name, shader-name を指定する。３つが同じで良い場合渡すラベルは１つで良い。
		* signature と shader は名前で引ける状態である必要がある。
		*/
		
		if (!InitDefaultPso("lighting", layoutVertexNormalTexcoord)) return E_FAIL;
		
		if (!InitDefaultPso("vat", "copyback", "vat", layoutVertex, PrimtiveType::Triangle, BlendType::None, DepthUse::ReadWrite))return E_FAIL;
		
		return hr;
	}

} // unnamed


namespace Scene {

	HRESULT Initialize(HWND hWnd, UINT width, UINT height)
	{
		// DirectX の初期化
		g_pDx12 = new Dx12Util();

		HRESULT hr = g_pDx12->Initialize(hWnd, width, height, DEBUG_LAYER);
		if (FAILED(hr)) return hr;

		// 描画環境を構築
		if (FAILED(hr = SetupSceneEnvironment(g_pDx12))) {
			Release();
			return hr;
		}

		// カメラ
		SetupCamera(g_camera, CAMERA_X, CAMERA_Y, CAMERA_Z, 0.1f, 20000, 35.0f);

		// ライト
		SetupCamera(g_light, LIGHT_X, LIGHT_Y, LIGHT_Z, 0.1f, 10000, 25.0f);

		

		//VAT
		{
			auto result = CreatePlaneWithVAT(g_pDx12, GetSignature("copyback"), GetPso("vat"), VAT_meshSize, 500.0f);
			if (!result.first || !result.second) {
				delete result.first;
				delete result.second;
				Release();
				return hr;
			}
			auto obj = result.first;
			obj->world[3][1] = -50.0f;
			g_objects["vat"] = obj;
			(void)AddTexture("vat", result.second);
		}

		// VAT を音声で動かす初期化（音量=振幅、ピッチ=速度）
		Scene::InitVATAudio();

		


		const AllocateSet lineStripAllocateSet{
			sizeof(ConstantBuffer::Float3Array100),
			sizeof(SimpleConstantBuffer::PixelColor),
			sizeof(SimpleLayout::Vertex),
			sizeof(uint16_t)
		};

#if 1
		///// 以下 fbx を読み込むなど独自の設定を行う /////
		for (auto& fbx : g_initialFbx) {
			auto signature = GetSignature(fbx.sig_pso);
			auto pso = GetPso(fbx.sig_pso);

			if (signature == nullptr) {
				std::string str = "signature " + fbx.sig_pso + " not defined";
				MessageBoxA(hWnd, str.c_str(), "Error", MB_OK);
				Release();
				return E_FAIL;
			}

			if (pso == nullptr) {
				std::string str = "pipeline state " + fbx.sig_pso + " not defined";
				MessageBoxA(hWnd, str.c_str(), "Error", MB_OK);
				Release();
				return E_FAIL;
			}

			ImportDesc desc;
			desc.inverseV = true;
			desc.filename = fbx.path;

			auto obj = Scene::LoadFBX(g_pDx12, desc, signature, pso, GetPsoDepth(fbx.sig_pso), !fbx.diffuseMap.empty(), hr);
			if (obj == nullptr) {
				if (hr == E_FAIL) {
					MessageBoxA(hWnd, desc.error.c_str(), "Error", MB_OK);
				}
				Release();
				return hr;
			}

			g_objects[fbx.name] = obj;

			std::string sig_shadow = "shadow_" + fbx.sig_pso;
			auto signatureShadow = g_pDx12->HasShadowMap() ? GetSignature(sig_shadow) : nullptr;
			auto psoShadow = g_pDx12->HasShadowMap() ? GetPso(sig_shadow) : nullptr;
			if (signatureShadow && psoShadow) {
				obj->AttachShadow(signatureShadow->Signature(), psoShadow);
			}

			// 定数データ
			obj->SetScale(fbx.scale);
			obj->SetPosition(fbx.position);

			// diffuseMap が指定されているなら、DrawFunc に関係なくテクスチャをバインドする
			if (!fbx.diffuseMap.empty()) {
				auto texture = new SimpleTexture(g_pDx12->Device(), fbx.diffuseMap);
				if (texture) {
					if (SUCCEEDED(Scene::BindTextureResource(g_pDx12->Device(), obj, texture->Resource()))) {
						AddTexture(fbx.diffuseMap, texture);   // key はパス文字列
					}
					else {
						delete texture;
					}
				}
			}

			// 影を受けるなら、影リソースも追加でバインドする（テクスチャと両立OK）
			if (obj->IsReceiveShadow()) {
				(void)Scene::BindShadowMapResource(g_pDx12->Device(), obj, g_pDx12->GetShadowMapDesc());
			}
		}
#endif

		return S_OK;
	}

	void Release()
	{
		// WinMM マイクデバイスを先に停止（コールバックスレッドを止めてからDX12解放）
		Scene::ShutdownVATAudio();

		ReleaseResources();

		for (auto& obj : g_objects) {
			obj.second->Release();
			delete obj.second;
		}
		g_objects.clear();

		delete g_pDx12;
		g_pDx12 = nullptr;
	}

	// 以下 WinMain 用のインターフェース
	// g_objects などの変数アクセスが楽なのでここに配置しておく

	void Resize(HWND hWnd)
	{
		if (g_pDx12) {
			RECT rect;
			GetClientRect(hWnd, &rect);
			auto w = rect.right - rect.left;
			auto h = rect.bottom - rect.top;
			g_pDx12->Resize(hWnd, w, h);
		}
	}

	void MouseButtonDown(UINT message, int x, int y)
	{
		if (g_camera.operation != message) {
			g_camera.operation = message;
			g_camera.op_x = x;
			g_camera.op_y = y;
		}
	}

	void MouseButtonUp(UINT message, int x, int y)
	{
		(void)message;
		(void)x;
		(void)y;
		g_camera.operation = 0;
	}

	void MouseMove(int x, int y)
	{
		if (g_camera.operation != 0) {
			g_camera.MouseMove(x, y);
		}
	}

	void MouseWheel(bool dir)
	{
		float z = dir ? 1.05f : 0.95f;
		float fovy = g_camera.fovy * z;
		if (fovy < 2.0f) fovy = 2.0f;
		if (fovy > 115.0f) fovy = 115.0f;
		g_camera.fovy = fovy;
	}

	void App(float durationSec)
	{
		if (g_pDx12) Scene::App(g_pDx12, durationSec, g_objects, g_camera, g_light);
	}

}	// Scene
