#pragma once

#include <cstdint>
#include <windows.h>
#include <string>
#include <vector>

#include "MathUtility.h"   // Vector2

// ════════════════════════════════════════════════════════════════════
// ウィンドウ関連をまとめたクラス
//   DirectX には一切依存しない。純粋な Win32 の担当
// ════════════════════════════════════════════════════════════════════
class WinApp {
public:
	// クライアント領域のサイズ
	// コンパイル時に確定し、今後定義することのない変数であるため static constexpr
	static constexpr int32_t kClientWidth = 1600; 
	static constexpr int32_t kClientHeight = 900;

	// ウィンドウプロシージャ
	//   Windows から直接呼ばれるので static でなければならない
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// ウィンドウの生成〜表示まで
	void Initialize();

	// メッセージを処理する。終了メッセージが来たら true を返す
	bool ProcessMessage();

	void Finalize();

	// ────────────────────────────────────────────────────────────────
	// ファイルのドラッグ&ドロップ
	//   ProcessMessage() を呼ぶたびに前フレームの分は捨てられる
	// ────────────────────────────────────────────────────────────────

	/// <summary>このフレームにドロップされたファイルの絶対パス一覧</summary>
	const std::vector<std::string>& GetDroppedFiles() const;

	/// <summary>ドロップされた位置（クライアント座標）</summary>
	Vector2 GetDropPosition() const;


	// 取得
	HWND      GetHwnd() const { return hwnd_; }
	HINSTANCE GetHInstance() const { return wc_.hInstance; }

private:
	HWND     hwnd_ = nullptr;   // ウィンドウハンドル
	WNDCLASS wc_{};             // ウィンドウクラス（登録情報）
};