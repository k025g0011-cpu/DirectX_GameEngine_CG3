#include "WinApp.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

// ════════════════════════════════════════════════════════════════════
// ウィンドウプロシージャ
//   Windows が呼ぶ。定義側に static は書かない（宣言側だけ）
// ════════════════════════════════════════════════════════════════════
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

#ifdef USE_IMGUI
	// ImGui に先にメッセージを渡す。ImGui が処理したならここで終わり
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif

	// メッセージに応じたゲーム固有の処理
	switch (msg) {
		// ウィンドウが破棄された
	case WM_DESTROY:
		// OS に対してアプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// ════════════════════════════════════════════════════════════════════
// 初期化（ウィンドウクラスの登録 〜 ウィンドウの表示）
// ════════════════════════════════════════════════════════════════════
void WinApp::Initialize() {

	// COM の初期化（WIC でのテクスチャ読み込みに必要）
	CoInitializeEx(0, COINIT_MULTITHREADED);

	// ────────────────────────────────────────────────────────────────
	// ① ウィンドウクラスの登録
	//     「こういう種類のウィンドウを作りますよ」と OS に申請する
	// ────────────────────────────────────────────────────────────────
	wc_.lpfnWndProc = WindowProc;                     // ウィンドウプロシージャ
	wc_.lpszClassName = L"CG2WindowClass";              // ウィンドウクラス名
	wc_.hInstance = GetModuleHandle(nullptr);       // インスタンスハンドル
	wc_.hCursor = LoadCursor(nullptr, IDC_ARROW); // カーソル

	RegisterClass(&wc_);

	// ────────────────────────────────────────────────────────────────
	// ② クライアント領域からウィンドウ全体のサイズを逆算
	// ────────────────────────────────────────────────────────────────
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ────────────────────────────────────────────────────────────────
	// ③ ウィンドウの生成
	// ────────────────────────────────────────────────────────────────
	hwnd_ = CreateWindow(
		wc_.lpszClassName,      // 利用するクラス名
		L"CG2",                 // タイトルバーの文字
		WS_OVERLAPPEDWINDOW,    // よく見るウィンドウスタイル
		CW_USEDEFAULT,          // 表示X座標（OS に任せる）
		CW_USEDEFAULT,          // 表示Y座標（OS に任せる）
		wrc.right - wrc.left,   // ウィンドウ横幅
		wrc.bottom - wrc.top,   // ウィンドウ縦幅
		nullptr,                // 親ウィンドウハンドル
		nullptr,                // メニューハンドル
		wc_.hInstance,          // インスタンスハンドル
		nullptr);               // オプション

	// ────────────────────────────────────────────────────────────────
	// ④ ウィンドウを表示する
	// ────────────────────────────────────────────────────────────────
	ShowWindow(hwnd_, SW_SHOW);
}

// ════════════════════════════════════════════════════════════════════
// メッセージの処理
//   溜まっているメッセージを全部処理する
//   戻り値 true = アプリを終了すべき
// ════════════════════════════════════════════════════════════════════
bool WinApp::ProcessMessage() {
	MSG msg{};

	// キューが空になるまで処理し続ける
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {

		// 終了メッセージなら即座に抜ける
		//   WM_QUIT は WindowProc に送っても意味がないので Dispatch しない
		if (msg.message == WM_QUIT) {
			return true;
		}

		TranslateMessage(&msg);   // キー入力を文字に変換
		DispatchMessage(&msg);    // WindowProc へ送る
	}

	return false;
}

// ════════════════════════════════════════════════════════════════════
// 後片付け
// ════════════════════════════════════════════════════════════════════
void WinApp::Finalize() {
	CloseWindow(hwnd_);
	CoUninitialize();
}