#include "stdafx.h"
#include "resource.h"
#include "zmouse.h"
#include <windowsx.h>

#include <stdlib.h>
#include <tchar.h>

#include "scene.h"
#include <vector>

#define MAX_LOADSTRING 100

#define WINDOW_WIDTH	1024	//ウィンドウ幅
#define WINDOW_HEIGHT	768		//ウィンドウ高さ

HINSTANCE hInst;                                // 現在のインターフェイス
WCHAR szTitle[MAX_LOADSTRING];                  // タイトル バーのテキスト
WCHAR szWindowClass[MAX_LOADSTRING];            // メイン ウィンドウ クラス名

struct MonitorInfo {
	HMONITOR hMonitor;
	MONITORINFOEX mi;
};

ATOM                MyRegisterClass(HINSTANCE hInstance);
HWND                InitInstance(HINSTANCE, int, const MonitorInfo &);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam)
{
	std::vector<MonitorInfo> *monitors = reinterpret_cast<std::vector<MonitorInfo> *>(lParam);

	MONITORINFOEX mi;
	mi.cbSize = sizeof(MONITORINFOEX);
	if(GetMonitorInfo(hMonitor, &mi)) {
		monitors->push_back({ hMonitor, mi });
	}
	return TRUE;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // グローバル文字列を初期化しています。
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_DX12_SIMPLE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

	std::vector<MonitorInfo> monitors;
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors));

	// サブモニタに表示させたいので monitors.back()
	// メインモニタが良ければ monitor[0]
	HWND hWnd = InitInstance(hInstance, nCmdShow, monitors.back());

    // アプリケーションの初期化を実行します:
    if(hWnd == 0) {
        return -1;
    }

	int res = Scene::Initialize(hWnd, WINDOW_WIDTH, WINDOW_HEIGHT);
	if(res) {
		return res;
	}

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DX12_SIMPLE));

	LONGLONG freq = 0, prev = 0;
	if(QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&freq))) {
		QueryPerformanceCounter((LARGE_INTEGER *)&prev);
	}
	
	MSG msg;
	// メイン メッセージ ループ:
	for(msg.message = 0; msg.message != WM_QUIT; )	{
		float duration = 0.0f;
		decltype(freq) cur = 0;
		if(freq != 0) {
			QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&cur));
			auto dur = cur - prev;
			duration = static_cast<float>(static_cast<double>(dur) / static_cast<double>(freq));
		}

		if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&msg);

		// 高リフレッシュレートだと motion blur が見辛いであろう
		if(duration > 0.016f) {
			Scene::App(duration);
			prev = cur;
		}
	}

	Scene::Release();

    return (int)msg.wParam;
}

//
//  関数: MyRegisterClass()
//
//  目的: ウィンドウ クラスを登録します。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style          = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc    = WndProc;
	wcex.cbClsExtra     = 0;
	wcex.cbWndExtra     = 0;
	wcex.hInstance      = hInstance;
	wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DX12_SIMPLE));
	wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DX12_SIMPLE);
	wcex.lpszClassName  = szWindowClass;
	wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

//
//   関数: InitInstance(HINSTANCE, int)
//
//   目的: インスタンス ハンドルを保存して、メイン ウィンドウを作成します。
//
HWND InitInstance(HINSTANCE hInstance, int nCmdShow, const MonitorInfo &monitor)
{
	hInst = hInstance; // グローバル変数にインスタンス処理を格納します。

	auto &monitorRect = monitor.mi.rcWork;

	// MonitorInfo に合わせて表示位置を調整する
	RECT rect;
	DWORD x = CW_USEDEFAULT, y = CW_USEDEFAULT;
	if(WINDOW_WIDTH >= monitorRect.right - monitorRect.left || WINDOW_HEIGHT >= monitorRect.bottom - monitorRect.top) {
		rect.left = rect.top = 0;
		rect.right = WINDOW_WIDTH;
		rect.bottom = WINDOW_HEIGHT;
	} else {
		x = ((monitorRect.right - monitorRect.left) - WINDOW_WIDTH) >> 1;
		y = ((monitorRect.bottom - monitorRect.top) - WINDOW_HEIGHT) >> 1;
		x += monitorRect.left;
		y += monitorRect.top;
		rect.left = x;
		rect.top = y;
		rect.right = rect.left + WINDOW_WIDTH;
		rect.bottom = rect.top + WINDOW_HEIGHT;
	}
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		x, y, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr);

	if(!hWnd) return 0;

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	return hWnd;
}

//
//  関数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目的:    メイン ウィンドウのメッセージを処理します。
//
//  WM_COMMAND  - アプリケーション メニューの処理
//  WM_PAINT    - メイン ウィンドウの描画
//  WM_DESTROY  - 中止メッセージを表示して戻る
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message) {
	case WM_SIZE:
		Scene::Resize(hWnd);
		break;

	case WM_COMMAND:
		{
			int wmId = LOWORD(wParam);
			// 選択されたメニューの解析:
			switch(wmId) {
			case IDM_ABOUT:
				DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
				break;
			case IDM_EXIT:
				DestroyWindow(hWnd);
				break;
			default:
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
		}
		break;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			EndPaint(hWnd, &ps);
		}
		break;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		Scene::MouseButtonDown(message, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		SetCapture(hWnd);
		break;

	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		Scene::MouseButtonUp(message, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		ReleaseCapture();
		break;

	case WM_MOUSEMOVE:
		Scene::MouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		break;

	case WM_MOUSEWHEEL:
		Scene::MouseWheel(HIWORD(wParam) < 32767);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// バージョン情報ボックスのメッセージハンドラー
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch(message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
