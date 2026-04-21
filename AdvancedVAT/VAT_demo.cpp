#include "stdafx.h"
#include "dx12_util.h"
#include "primitive.h"
#include "texture.h"

// --- mic analyzer (WinMM) ---------------------------------------------
// VAT ・ｽﾌ「・ｽU・ｽ・ｽ・ｽv・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ(RMS)・ｽﾅ、・ｽu・ｽﾄ撰ｿｽ・ｽ・ｽ・ｽx・ｽv・ｽ・ｽ・ｽs・ｽb・ｽ`(Hz)・ｽﾅ托ｿｽ・ｽ・ｷ・ｽ・ｽB
// NOTE:
//  - ・ｽs・ｽb・ｽ`・ｽ・ｽ・ｽ・ｽﾍゼ・ｽ・ｽ・ｽN・ｽ・ｽ・ｽX・ｽ@(・ｽG)・ｽB・ｽm・ｽC・ｽY・ｽﾉは弱い・ｽB
//  - ・ｽ・ｽ・ｽm・ｽ・ｽ・ｽ・ｽ・ｽg・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽﾆ”・ｽD・ｽ・ｽB

#include <Windows.h>
#include <mmsystem.h>
#include <atomic>
#include <vector>
#include <cstdint>
#include <cmath>

#pragma comment(lib, "winmm.lib")

namespace Scene {

	namespace {

		static inline float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}

		static inline float Lerp(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		// RMS(0..~0.2) -> ・ｽU・ｽ・ｽ・ｽW・ｽ・ｽ(0..2)
		static float RmsToAmp(float rms)
		{
			// ・ｽﾂ具ｿｽ・ｽﾉゑｿｽ・ｽS・ｽR・ｽﾏゑｿｽ・ｽﾌで、・ｽ・ｽ・ｽ・ｽ・ｽ?調撰ｿｽ・ｽ・ｽ・ｽﾄ“・ｽC・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽh・ｽﾍ囲に搾ｿｽ・ｽ?椹・ｽ・ｽ
			const float minRms = 0.004f; // 讀懃衍髢句ｧ九・荳矩剞
			const float maxRms = 0.02f;  // 譛?螟ｧ謖ｯ蟷・・荳企剞
			float t = (rms - minRms) / (maxRms - minRms);
			t = Clamp(t, 0.0f, 1.0f);
			t = sqrtf(t);
			return t * 5.0f; // 0.0 ・ｽ` 2.0
		}

		static float HzToSpeed(float hz)
		{
			// ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽﾌ範茨ｿｽ(・ｽj・ｽ・ｽ・ｽ`・ｽ・ｽ・ｽ・ｽ)
			const float minHz = 80.0f;
			const float maxHz = 250.0f;  // 400->250 Hz
			float t = (hz - minHz) / (maxHz - minHz);
			t = Clamp(t, 0.0f, 1.0f);
			t = t * t * t;              // cubic: high pitch -> much faster
			return 0.05f + t * 14.95f;  // 0.05~15.0 (was 0.25~4.0) // 0.25 ・ｽ` 4.0 (1・ｽt・ｽ・ｽ・ｽ[・ｽ・ｽ・ｽﾅ進・ｽﾞ暦ｿｽ)
		}

		class VATAudio
		{
		public:
			static void Init()
			{
				if (s_inited.load()) return;

				WAVEFORMATEX wf{};
				wf.wFormatTag = WAVE_FORMAT_PCM;
				wf.nChannels = 1;
				wf.nSamplesPerSec = kSampleRate;
				wf.wBitsPerSample = 16;
				wf.nBlockAlign = (wf.nChannels * wf.wBitsPerSample) / 8;
				wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

				MMRESULT r = waveInOpen(&s_hWaveIn, WAVE_MAPPER, &wf,
					(DWORD_PTR)&VATAudio::WaveInProc, 0, CALLBACK_FUNCTION);
				if (r != MMSYSERR_NOERROR) {
					s_hWaveIn = nullptr;
					return;
				}

				s_buffers.resize(kNumBuffers);
				s_headers.resize(kNumBuffers);
				for (int i = 0; i < kNumBuffers; ++i) {
					s_buffers[i].resize(kSamplesPerBuffer);
					WAVEHDR& hdr = s_headers[i];
					ZeroMemory(&hdr, sizeof(hdr));
					hdr.lpData = (LPSTR)s_buffers[i].data();
					hdr.dwBufferLength = (DWORD)(kSamplesPerBuffer * sizeof(int16_t));
					waveInPrepareHeader(s_hWaveIn, &hdr, sizeof(hdr));
					waveInAddBuffer(s_hWaveIn, &hdr, sizeof(hdr));
				}

				waveInStart(s_hWaveIn);
				s_inited.store(true);
			}

			static void Update()
			{
				if (!s_inited.load()) return;

				const float rms = s_lastRms.load();
				const float hz = s_lastHz.load();

				// ・ｽ・ｽ・ｽ・ｽ・ｽQ・ｽ[・ｽg・ｽF・ｽ・ｽ・ｽ・ｽ・ｽﾉ近ゑｿｽ・ｽﾈゑｿｽU・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽx・ｽ・ｽ・ｽ・ｽ・ｽﾆゑｿｽ・ｽi・ｽ・ｽ・ｽ・ｽ・ｽﾍ好・ｽﾝ）
				const bool silent = (rms < 0.003f);
				const float ampTarget = silent ? 0.0f : RmsToAmp(rms);
				const float speedTarget = silent ? 0.0f : HzToSpeed(hz);

				// ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽi・ｽK・ｽN・ｽK・ｽN・ｽh・ｽ~・ｽj
				const float ampSmooth   = 0.2f;
				const float speedSmooth = 0.35f; // speed reacts faster than amp
				s_amp.store(Lerp(s_amp.load(), ampTarget, ampSmooth));
				s_speed.store(Lerp(s_speed.load(), speedTarget, speedSmooth));
			}

			static void Shutdown()
			{
				if (!s_inited.load()) return;
				s_inited.store(false);
				if (s_hWaveIn) {
					waveInStop(s_hWaveIn);
					waveInReset(s_hWaveIn); // 譛ｪ蜃ｦ逅・ヰ繝・ヵ繧｡縺ｮ繧ｳ繝ｼ繝ｫ繝舌ャ繧ｯ繧貞ｮ御ｺ・＆縺帙ｋ
					for (int i = 0; i < kNumBuffers; ++i) {
						waveInUnprepareHeader(s_hWaveIn, &s_headers[i], sizeof(WAVEHDR));
					}
					waveInClose(s_hWaveIn);
					s_hWaveIn = nullptr;
				}
				s_buffers.clear();
				s_headers.clear();
			}

			static float GetAmp() { return s_amp.load(); }
			static float GetSpeed() { return s_speed.load(); }
			static float GetHz() { return s_lastHz.load(); }
			static float GetRms() { return s_lastRms.load(); }

		private:
			static void CALLBACK WaveInProc(HWAVEIN hwi, UINT msg, DWORD_PTR, DWORD_PTR p1, DWORD_PTR)
			{
				if (msg != WIM_DATA) return;
				if (!hwi) return;

				WAVEHDR* hdr = (WAVEHDR*)p1;
				if (!hdr || hdr->dwBytesRecorded == 0) {
					waveInAddBuffer(hwi, hdr, sizeof(WAVEHDR));
					return;
				}

				const int16_t* samples = (const int16_t*)hdr->lpData;
				const int count = (int)(hdr->dwBytesRecorded / sizeof(int16_t));

				// RMS
				double acc = 0.0;
				for (int i = 0; i < count; ++i) {
					double s = (double)samples[i];
					acc += s * s;
				}
				const double mean = (count > 0) ? (acc / (double)count) : 0.0;
				float rms = (float)std::sqrt(mean) / 32768.0f;
				s_lastRms.store(rms);

				// Pitch (zero-cross)
				// ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽm・ｽC・ｽY・ｽﾅ鯉ｿｽ・ｽ・ｽ・ｽJ・ｽE・ｽ・ｽ・ｽg・ｽ・ｽ・ｽﾈゑｿｽ・ｽ謔､・ｽ・ｽ閾値・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ
				const int16_t th = 256;
				int crossings = 0;
				int16_t prev = samples[0];
				for (int i = 1; i < count; ++i) {
					int16_t cur = samples[i];
					// ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ]・ｽi閾値・ｽt・ｽ・ｽ・ｽj
					const bool prevNeg = (prev < -th);
					const bool prevPos = (prev > th);
					const bool curNeg = (cur < -th);
					const bool curPos = (cur > th);
					if ((prevNeg && curPos) || (prevPos && curNeg)) {
						++crossings;
					}
					prev = cur;
				}

				float hz = 0.0f;
				if (crossings >= 2 && count > 0) {
					hz = (float)crossings * (float)kSampleRate / (2.0f * (float)count);
				}
				s_lastHz.store(hz);

				// 繧ｷ繝｣繝・ヨ繝?繧ｦ繝ｳ荳ｭ縺ｧ縺ｪ縺代ｌ縺ｰ繝舌ャ繝輔ぃ繧貞・繧ｭ繝･繝ｼ
				if (s_inited.load()) {
					waveInAddBuffer(hwi, hdr, sizeof(WAVEHDR));
				}
			}

		private:
			static constexpr int kSampleRate = 44100;
			static constexpr int kSamplesPerBuffer = 2048;
			static constexpr int kNumBuffers = 4;

			static std::atomic<bool> s_inited;
			static HWAVEIN s_hWaveIn;
			static std::vector<std::vector<int16_t>> s_buffers;
			static std::vector<WAVEHDR> s_headers;

			static std::atomic<float> s_lastRms;
			static std::atomic<float> s_lastHz;
			static std::atomic<float> s_amp;
			static std::atomic<float> s_speed;
		};

		std::atomic<bool> VATAudio::s_inited{ false };
		HWAVEIN VATAudio::s_hWaveIn = nullptr;
		std::vector<std::vector<int16_t>> VATAudio::s_buffers;
		std::vector<WAVEHDR> VATAudio::s_headers;
		std::atomic<float> VATAudio::s_lastRms{ 0.0f };
		std::atomic<float> VATAudio::s_lastHz{ 0.0f };
		std::atomic<float> VATAudio::s_amp{ 0.0f };
		std::atomic<float> VATAudio::s_speed{ 0.0f };

	} // unnamed

	// Scene 縺九ｉ菴ｿ縺・・髢帰PI
	void InitVATAudio() { VATAudio::Init(); }
	void ShutdownVATAudio() { VATAudio::Shutdown(); }
	void UpdateVATAudio() { VATAudio::Update(); }
	float GetVATAmp() { return VATAudio::GetAmp(); }
	float GetVATSpeed() { return VATAudio::GetSpeed(); }
	float GetVATHz() { return VATAudio::GetHz(); }
	float GetVATRms() { return VATAudio::GetRms(); }

	void CalcNormal(float4* pbuf, std::vector<SimpleLayout::Vertex>& vertices, const size_t size)
	{
		// ・ｽ・ｽ・ｽ_・ｽﾌ位置・ｽ・ｽﾔゑｿｽ
		auto getPos = [pbuf, vertices, size](size_t x, size_t z) {
			const DirectX::XMFLOAT3& a = vertices[z * size + x].vx;
			const auto p = DirectX::XMLoadFloat3(&a);
			const float y = pbuf[z * size + x][3];
			return DirectX::XMVectorSet(DirectX::XMVectorGetX(p), DirectX::XMVectorGetY(p) + y, DirectX::XMVectorGetZ(p), 0);
			};

		for (size_t z = 0; z < size; z++) {
			for (size_t x = 0; x < size; x++) {
				// ・ｽ・ｽ・ｽ・ｽ・ｽﾉは趣ｿｽ・ｽﾍ全・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽﾗゑｿｽ・ｽ・ｽ・ｽ・ｽ・ｽA・ｽ・ｽ・ｽ・ｽ・ｽﾍ鯉ｿｽ・ｽ・ｽ・ｽﾚ優・ｽ・ｽ
				auto xv = DirectX::XMVectorSubtract(getPos(x, z), getPos(x > 0 ? x - 1 : x + 1, z));
				auto zv = DirectX::XMVectorSubtract(getPos(x, z), getPos(x, z > 0 ? z - 1 : z + 1));
				auto nv = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(zv, xv));
				if (DirectX::XMVectorGetY(nv) < 0.0f) nv = DirectX::XMVectorNegate(nv);

				float4* base = &pbuf[z * size];
				base[x][0] = DirectX::XMVectorGetX(nv);
				base[x][1] = DirectX::XMVectorGetY(nv);
				base[x][2] = DirectX::XMVectorGetZ(nv);
			}
		}
	}

	/*
	*  0: 0  1  2  ...  N-1
	*  1: 0  1  2  ...  N-1
	*  .  .
	*  .  .
	* N-1 0  1  2  ...  N-1
	*/

	std::pair<SimpleObject*, SimpleTexture*> CreatePlaneWithVAT(Dx12Util* pDx12, RootSignature* sig, ID3D12PipelineState* pso, UINT N, float scale)
	{
		const UINT numPoints = N * N;
		const UINT numIndices = (N - 1) * (N - 1) * 6;

		SimpleObject* obj = new SimpleObject(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST, DrawFunc::LIT_TEX, sig, pso);
		if (FAILED(obj->Allocate(pDx12,
			sizeof(SimpleConstantBuffer::ProjectionModelviewWorld),
			sizeof(SimpleConstantBuffer::PixelLightingSet),
			sizeof(SimpleLayout::Vertex) * numPoints,
			sizeof(uint16_t) * numIndices)))
		{
			return std::make_pair(nullptr, nullptr);
		}

		// VAT ・ｽﾉ法・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽﾌで・ｿｽ・ｽC・ｽA・ｽE・ｽg・ｽﾍ抵ｿｽ・ｽ_・ｽﾌゑｿｽ
		std::vector<SimpleLayout::Vertex> vertices(numPoints);
		std::vector<uint16_t> indices(numIndices);

		// vertices (XZ ・ｽ・ｽ・ｽﾊの費ｿｽ)
		{
			const float step = scale / static_cast<float>(N - 1);
			float z = -0.5f * scale;
			for (UINT i = 0, n = 0; i < N; i++) {
				float x = -0.5f * scale;
				for (UINT j = 0; j < N; j++) {
					vertices[n++] = { DirectX::XMFLOAT3(x, 0, z) };
					x += step;
				}
				z += step;
			}
		}

		// indices (triangle list)
		{
			for (UINT z = 0, n = 0; z < (N - 1); z++) {
				UINT x = z * N;
				for (UINT i = 0; i < (N - 1); i++, x++) {
					indices[n++] = x;
					indices[n++] = x + 1;
					indices[n++] = x + N;
					indices[n++] = x + 1;
					indices[n++] = x + N;
					indices[n++] = x + N + 1;
				}
			}
		}

		// set vertex buffer
		pDx12->Set(obj->pVertexBuffer, vertices.data(), vertices.size() * sizeof(SimpleLayout::Vertex));
		obj->vertexBufferView.BufferLocation = obj->pVertexBuffer->GetGPUVirtualAddress();
		obj->vertexBufferView.StrideInBytes = sizeof(SimpleLayout::Vertex);
		obj->vertexBufferView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(SimpleLayout::Vertex));
		// set index buffer
		pDx12->Set(obj->pIndexBuffer, indices.data(), indices.size() * sizeof(uint16_t));
		obj->SetIndexCount(static_cast<UINT>(indices.size()));
		obj->indexBufferView.BufferLocation = obj->pIndexBuffer->GetGPUVirtualAddress();
		obj->indexBufferView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(uint16_t));
		obj->indexBufferView.Format = DXGI_FORMAT_R16_UINT;

		// texture
		const UINT width = N * N;	// ・ｽe・ｽN・ｽX・ｽ`・ｽ・ｽ1・ｽ・ｽ・ｽC・ｽ・ｽ・ｽﾉ全・ｽ・ｽ・ｽ_・ｽﾌ擾ｿｽ・ｽ
		const UINT height = N * N;	// VAT・ｽﾌフ・ｽ・ｽ・ｽ[・ｽ・ｽ・ｽ・ｽ・ｽi・ｽ・ｽ・ｽ・ｽ・ｽ`・ｽ・ｽ・ｽ・ｽ・ｽj
		// RGBA_Float ・ｽﾅ搾ｿｽ・ｽ
		std::vector<float4> texture(width * height);

		// ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ ・ｽg・ｽ?ｫ?・ｽU・ｽ・ｽ・ｽh ・ｽﾆゑｿｽ・ｽﾄ難ｿｽ・ｽ・ｽﾄゑｿｽ・ｽ・ｽ・ｽB・ｽ・ｽ・ｽﾛの托ｿｽ・ｽ・ｽ・ｽ・ｽ HLSL ・ｽ・ｽ・ｽ・ｽ amp ・ｽ・ｽ・ｽ|・ｽ・ｽ・ｽ・ｽB
		const float baseAmp = scale * 0.1f;

		const size_t tableSize = (N * N) >> 3;
		const size_t repeat = 6;
		std::vector<float> sinTable(tableSize * repeat);

		const float step = 2.0f * PIf / static_cast<float>(tableSize);
		float t = 0.0f;
		for (size_t i = 0; i < tableSize; i++) {
			sinTable[i] = sinf(t) * baseAmp;
			t += step;
		}
		for (size_t i = 1; i < repeat; i++) {
			memcpy(&sinTable[tableSize * i], &sinTable[0], tableSize * sizeof(float));
		}

		// ・ｽT・ｽC・ｽ・ｽ・ｽe・ｽ[・ｽu・ｽ・ｽ・ｽ・ｽ・ｽe・ｽN・ｽX・ｽ`・ｽ・ｽ・ｽﾉ擾ｿｽ・ｽ・ｽ・ｽ・ｽ・ｽﾞ関撰ｿｽ
		auto copyRange = [&sinTable, &texture](int top, int length, int offset) {
			// ・ｽ・ｽ・ｽ・ｽ・ｽl・ｽN・ｽ・ｽ・ｽA
			for (int i = 0, p = top; i < length; i++, p++) {
				texture[p][0] = 0;
				texture[p][1] = 1;
				texture[p][2] = 0;
				texture[p][3] = 0;
			}

			const int tableSize = static_cast<int>(sinTable.size());
			for (int i = 0, p = top + offset; i < tableSize && p < (top + length); i++, p++) {
				if (p >= top) {
					texture[p][3] = sinTable[i];
				}
			}
			};

		// texture: RGB ・ｽ・ｽ normal, A ・ｽﾉ搾ｿｽ・ｽ・ｽ
		int offset = -static_cast<int>(sinTable.size());
		for (UINT frame = 0; frame < height; frame++) {
			int timeline = static_cast<int>(frame * width);
			for (UINT i = 0; i < N; i++) {
				copyRange(timeline + i * N, static_cast<int>(N), offset - i * 3);
			}
			++offset;

			// 1・ｽ・ｽ・ｽC・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽﾌで法・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ・ｽ
			CalcNormal(&texture[timeline], vertices, N);
		}

		auto tex = new SimpleTexture();
		if (!tex->LoadTextureFromMemory(pDx12->Device(), texture, width, height)) {
			delete obj;
			return std::make_pair(nullptr, nullptr);
		}
		pDx12->CommitTexture(tex);

		return std::make_pair(obj, tex);
	}

} // namespace Scene