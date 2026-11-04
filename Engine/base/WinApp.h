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
	// ウィンドウモードで起動したときの既定サイズ
	//   全画面のときはモニタの解像度を使うので、こちらは使わない
	static constexpr int32_t kDefaultClientWidth = 1600;
	static constexpr int32_t kDefaultClientHeight = 900;

	// ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	/// <summary>ウィンドウの生成〜表示まで</summary>
	/// <param name="fullscreen">true ならボーダレス全画面。false なら既定サイズのウィンドウ</param>
	void Initialize(bool fullscreen = true);

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

	// ────────────────────────────────────────────────────────────────
	// クライアント領域のサイズ
	//   Initialize() で確定する。以後は変わらない
	// GetClientWidth()  / GetClientHeight()  → int32_t が欲しい人（DirectX の Desc 系）
	// GetClientSize()                        → Vector2 が欲しい人（2D の計算）
	// GetAspectRatio()                       → 比率だけ欲しい人（3D のカメラ）
	// ────────────────────────────────────────────────────────────────
	int32_t GetClientWidth() const { return clientWidth_; }
	int32_t GetClientHeight() const { return clientHeight_; }

	/// <summary>クライアント領域のサイズ（float 版）。2D の射影行列などに使う</summary>
	Vector2 GetClientSize() const {
		return {
			static_cast<float>(clientWidth_),
			static_cast<float>(clientHeight_),
		};
	}

	/// <summary>横÷縦。射影行列を作るときに使う</summary>
	float GetAspectRatio() const {
		return static_cast<float>(clientWidth_) / static_cast<float>(clientHeight_);
	}

	/// <summary>全画面で起動したか</summary>
	bool IsFullscreen() const { return isFullscreen_; }

private:
	HWND     hwnd_ = nullptr;   // ウィンドウハンドル
	WNDCLASS wc_{};             // ウィンドウクラス（登録情報）

	// 実際のクライアント領域サイズ。Initialize() で決まる
	int32_t clientWidth_ = kDefaultClientWidth;
	int32_t clientHeight_ = kDefaultClientHeight;

	bool isFullscreen_ = false;
};