#include "WinApp.h"
#include "Logger.h"      // ConvertString
#include <shellapi.h>    //（DragAcceptFiles / DragQueryFile）
#include <objbase.h>    // CoInitializeEx / CoUninitialize / COINIT_MULTITHREADED

#pragma comment(lib, "shell32.lib")   
#ifdef USE_IMGUI
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

// ════════════════════════════════════════════════════════════════════
// このファイルの中だけで使うデータ
//   WindowProc は static（コールバック）なので、
//   WinApp のメンバ変数に直接触れない。
//   ウィンドウは1つしかないので、ここに置くのが一番素直
// ════════════════════════════════════════════════════════════════════
namespace {

	// このフレームにドロップされたファイルの絶対パス
	std::vector<std::string> droppedFiles;

	// ドロップされた位置（クライアント座標）
	Vector2 dropPosition{};

}   // namespace

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
		// ────────────────────────────────────────────────────────────────
		// ファイルがドロップされた
		// ────────────────────────────────────────────────────────────────
	case WM_DROPFILES: {

		// wparam がドロップされたデータへのハンドル
		HDROP hDrop = reinterpret_cast<HDROP>(wparam);

		// ① 何個落とされたか
		//    第2引数に 0xFFFFFFFF を渡すと「個数」が返る仕様
		UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

		// ② 1個ずつパスを取り出す
		for (UINT i = 0; i < fileCount; ++i) {
			wchar_t path[MAX_PATH]{};
			DragQueryFileW(hDrop, i, path, MAX_PATH);

			// wchar_t* → std::string（既存の変換を使う）
			droppedFiles.push_back(ConvertString(path));
		}

		// ③ どこに落とされたか（クライアント座標）
		POINT point{};
		DragQueryPoint(hDrop, &point);
		dropPosition = { static_cast<float>(point.x), static_cast<float>(point.y) };

		// ④ 後始末。必須（忘れるとメモリリーク）
		DragFinish(hDrop);

		return 0;
	}
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

	// ファイルのドロップを受け付けるようにする
	DragAcceptFiles(hwnd_, TRUE);

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

	// 前フレームのドロップ情報を捨てる
	// これにより GetDroppedFiles() は「このフレームに落とされた分」になる
	droppedFiles.clear();

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
// ドロップされたファイル
// ════════════════════════════════════════════════════════════════════
const std::vector<std::string>& WinApp::GetDroppedFiles() const {
	return droppedFiles;
}

Vector2 WinApp::GetDropPosition() const {
	return dropPosition;
}


// ════════════════════════════════════════════════════════════════════
// 後片付け
// ════════════════════════════════════════════════════════════════════
void WinApp::Finalize() {
	DestroyWindow(hwnd_); // ウィンドウを破棄する
	CoUninitialize();
}