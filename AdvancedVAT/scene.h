#pragma once

// WinMain からのコントロールセット

namespace Scene {

HRESULT Initialize(HWND hWnd, UINT width, UINT height);
void Release();
void Resize(HWND hWnd);
void MouseButtonDown(UINT message, int x, int y);
void MouseButtonUp(UINT message, int x, int y);
void MouseMove(int x, int y);
void MouseWheel(bool dir);
void App(float durationSec);

}
