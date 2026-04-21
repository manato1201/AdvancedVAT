#include "stdafx.h"
#include "dx12_util.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"


HRESULT Dx12Util::ImGuiInitialize(HWND hWnd)
{
	HRESULT hr = E_FAIL;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();

	if(nullptr == io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/meiryo.ttc", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese())) {
		return E_FAIL;
	}

	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(hWnd);

	D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
	srvDesc.Type		   = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvDesc.NumDescriptors = 1;		  // フォント用
	srvDesc.Flags		  = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvDesc.NodeMask	   = 0;
	hr = m_pDevice->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_pImguiSrvHeap));

	if(SUCCEEDED(hr)) {
		auto cpuHandle = m_pImguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
		auto gpuHandle = m_pImguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
		UINT descIncr  = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		if(ImGui_ImplDX12_Init(
			m_pDevice, static_cast<int>(m_frameCount), m_rtvFormat,
			m_pImguiSrvHeap,
			cpuHandle,
			gpuHandle))
		{
			io.Fonts->Build();
			//ImGui_ImplDX12_InvalidateDeviceObjects();
			ImGui_ImplDX12_CreateDeviceObjects();
		} else {
			hr = E_FAIL;
		}
	}

	return hr;
}


void Dx12Util::ImGuiBegin()
{
	if(m_pImguiSrvHeap) {
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}
}

void Dx12Util::ImGuiEnd()
{
	if(m_pImguiSrvHeap) {
		//ImGui::Begin("Test Window");
		//ImGui::Text("Hello, ImGui!");
		//ImGui::Text("日本語テスト");
		//ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
		//ImGui::End();

		auto pCommandList = CommandList();

		ImGui::Render();

		// ImGui の SRV ヒープをセット
		ID3D12DescriptorHeap *heaps[] = {m_pImguiSrvHeap};
		pCommandList->SetDescriptorHeaps(1, heaps);

		// 描画コマンドを積む
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);
	}
}

void Dx12Util::DrawText(float x, float y, const char *str, float size, uint8_t r, uint8_t g, uint8_t b)
{
	if(m_pImguiSrvHeap) {
		auto *draw = ImGui::GetForegroundDrawList(); // 最前面。背面に描きたいなら GetBackgroundDrawList()
		draw->AddText(
			ImGui::GetFont(),                   // 省略可：nullptr なら現在のフォント
			ImGui::GetFontSize() * size,               // 省略可：0 で現在のフォントサイズ
			ImVec2(x, y - ImGui::GetFontBaked()->Ascent),                     // 画面左上からのピクセル座標
			IM_COL32(r,g,b,255),          // 色（RGBA）
			//u8"Hello, ImGui! ほげ"                     // 文字列（UTF-8）
			str
		);
	}
}
