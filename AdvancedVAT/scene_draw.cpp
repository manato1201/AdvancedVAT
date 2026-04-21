#include "stdafx.h"
#include "dx12_util.h"
#include "primitive.h"
#include "scene_util.h"
#include "scene_resources.h"
#include "shadow.h"
#include "offscreen.h"
#include "camera.h"
#include "texture.h"
#include <map>
#include <string>
#include <Windows.h> // GetTickCount64 用

#define	HANDLE_RADIUS	10

namespace Scene {

	template<typename T> void UpdateConstantBuffer(Dx12Util* pDx12, ID3D12Resource* resource, const T& data)
	{
		if (resource) pDx12->Set(resource, &data, sizeof(T));
	}

	// VAT_demo.cpp (mic control)
	void UpdateVATAudio();
	float GetVATAmp();
	float GetVATSpeed();

	Scene::FourPoints g_splineControlPoints = {
		{-250, -200},
		{-150,  100},
		{ 150, -100},
		{ 250,  200}
	};


	




	void AppVAT(Dx12Util* pDx12, std::map<std::string, SimpleObject*>& objects, SimpleCamera& camera, SimpleCamera& light, SimpleTexture* texture)
	{
		const float textureSize = static_cast<float>(VAT_meshSize * VAT_meshSize);
		static float vatFrame = 0;

		// 音声入力を更新
		Scene::UpdateVATAudio();
		const float vatAmp = Scene::GetVATAmp();
		const float vatSpeed = Scene::GetVATSpeed();
		auto pCommandList = pDx12->CommandList();
		pDx12->Begin(0, 0, 0.4f);
		// メイン描画
		Scene::SetViewport(pCommandList, pDx12->Width(), pDx12->Height());
		for (auto& objIt : objects) {
			auto obj = objIt.second;
			if (obj->IsHide()) continue;
			if (SUCCEEDED(BindTextureResource(pDx12->Device(), obj, texture->Resource(), texture->Format()))) {
				auto cBufferPS = obj->MapConstantBufferPS<SimpleConstantBuffer::PixelLightingSet>();
				if (!cBufferPS) continue;
				// ピクセルシェーダーのコンスタントバッファに書き出す

				// --- dithering gate (音が動いてる時だけ) ----------------------
				const float amp = vatAmp;
				const float speed = vatSpeed;

				// “音で動いてる時だけ” ディザを強める（閾値は適当に調整してOK）
				float ditherStrength = 0.0f;
				{
					const float th = 0.005f; // 無音扱いの閾値
					if (amp > th) {
						ditherStrength = (amp - th) / (1.0f - th);
						if (ditherStrength > 1.0f) ditherStrength = 1.0f;
					}
				}

				auto& lightPos = light.eye;

				// LightPos.w を「ディザリングのアニメ用time」に流用
				const float timeSec = static_cast<float>(GetTickCount64() * 0.001);
				cBufferPS->lightPos = DirectX::XMVectorSet(lightPos[0], lightPos[1], lightPos[2], timeSec);

				cBufferPS->diffuseColor = DirectX::XMVectorSet(1, 1, 1, 1);

				auto& cameraPos = camera.eye;

				// CameraPos.w を「ディザ強度」に流用（pad不要）
				cBufferPS->cameraPos = DirectX::XMVectorSet(cameraPos[0], cameraPos[1], cameraPos[2], ditherStrength);

				cBufferPS->intensity = 0.6f;
				cBufferPS->power = 64.0f;


				obj->UnmapPS(cBufferPS);
				// 画面サイズ、カメラ情報を更新するユーティリティ (obj->matrices がセットされる)
				obj->UpdateMatricesForConstantBuffer(pDx12->Width(), pDx12->Height(), camera);
				// 頂点シェーダーのコンスタントバッファに書き出す
				UpdateConstantBuffer(pDx12, obj->GetConstantBufferVS(), obj->matrices);
				// それとは別に追加情報を..
				auto cBufferVS = obj->MapConstantBufferVS<SimpleConstantBuffer::ProjectionModelviewWorld>();
				if (!cBufferVS) continue;
				// lightViewProj を別用途に使い回す
				// Params = (width, height, frame, amp)
				cBufferVS->lightViewProj.r[0] = DirectX::XMVectorSet(textureSize, textureSize, vatFrame, vatAmp);
				obj->UnmapVS(cBufferVS);
				obj->DrawCommand(pCommandList);
			}
		}
		vatFrame += vatSpeed;
		if (vatFrame >= textureSize) vatFrame = 0;
		pDx12->End();
	}

	


	void App(Dx12Util* pDx12, float durationSec, std::map<std::string, SimpleObject*>& objects, SimpleCamera& camera, SimpleCamera& light)
	{
		pDx12->BeginFrame();

		
		AppVAT(pDx12, objects, camera, light, GetTexture("vat"));
		

		pDx12->EndFrame();
	}

} // Scene
