#pragma once

#include <cstdint>
#include <windows.h>

// ════════════════════════════════════════════════════════════════════
// ウィンドウ関連をまとめたクラス
//   DirectX には一切依存しない。純粋な Win32 の担当
// ════════════════════════════════════════════════════════════════════
class WinApp {
public:
	// クライアント領域のサイズ
	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

	// ウィンドウプロシージャ
	//   Windows から直接呼ばれるので static でなければならない
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// ウィンドウの生成〜表示まで
	void Initialize();

	// メッセージを処理する。終了メッセージが来たら true を返す
	bool ProcessMessage();

	// 後片付け
	void Finalize();

	// 取得
	HWND      GetHwnd() const { return hwnd_; }
	HINSTANCE GetHInstance() const { return wc_.hInstance; }

private:
	HWND     hwnd_ = nullptr;   // ウィンドウハンドル
	WNDCLASS wc_{};             // ウィンドウクラス（登録情報）
};