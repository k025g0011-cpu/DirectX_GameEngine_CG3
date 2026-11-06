#pragma once

#include <Windows.h>

// ════════════════════════════════════════
// DIRECTINPUT_VERSION は必ず dinput.h より前に定義すること
// #ifndef で囲むことで、他のファイルで既に定義されていても警告が出ない
// ════════════════════════════════════════
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800   // DirectInput 8.00 を使う
#endif
#include <dinput.h>

#include <wrl.h>
#include <cstdint>

#include "MathUtility.h"   // Vector2

// ════════════════════════════════════════
// 入力（キーボード・マウス）
//   DirectInput8 を隠蔽し、押下状態とトリガーを提供する
// ════════════════════════════════════════
class Input {
public:
	// ───── 初期化・更新 ─────

	/// <summary>初期化。DirectInputの窓口とデバイスを生成する</summary>
	/// <param name="hInstance">アプリのインスタンスハンドル（wc.hInstance）</param>
	/// <param name="hwnd">入力を受け付けるウィンドウのハンドル</param>
	void Initialize(HINSTANCE hInstance, HWND hwnd);

	/// <summary>毎フレームの状態更新。ゲームループの先頭で1回だけ呼ぶ</summary>
	void Update();

	// ───── キーボード ─────
	// keyNumber には DIK_W のような DIK_ 定数を渡す
	// uint8_t なので 0〜255 に制限され、配列外アクセスが起きない

	/// <summary>押している状態か</summary>
	bool PushKey(uint8_t keyNumber) const;
	/// <summary>離している状態か</summary>
	bool ReleaseKey(uint8_t keyNumber) const;
	/// <summary>押した瞬間か（前:OFF → 今:ON）</summary>
	bool TriggerKey(uint8_t keyNumber) const;
	/// <summary>離した瞬間か（前:ON → 今:OFF）</summary>
	bool ReleaseTriggerKey(uint8_t keyNumber) const;

	// ───── マウスボタン ─────
	// buttonNumber : 0=左  1=右  2=中(ホイール押し込み)  3〜7=サイドボタン

	bool PushMouseButton(int32_t buttonNumber) const;
	bool ReleaseMouseButton(int32_t buttonNumber) const;
	bool TriggerMouseButton(int32_t buttonNumber) const;
	bool ReleaseTriggerMouseButton(int32_t buttonNumber) const;

	// ───── マウスの移動量 ─────
	// 絶対座標ではなく「前フレームからの差分」。動かしていないフレームは 0

	/// <summary>マウスの移動量（ピクセル相当の差分）</summary>
	Vector2 GetMouseMove() const;
	/// <summary>ホイールの回転量（1ノッチで ±1.0）</summary>
	float GetMouseWheel() const;

private:
	// ヘッダでは using を使わず完全修飾で書く（後述）
	Microsoft::WRL::ComPtr<IDirectInput8>       directInput_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouseDevice_;

	// 全キーの状態（押下中 0x80 / 非押下 0x00）
	BYTE key_[256] = {};   // 今フレーム
	BYTE keyPre_[256] = {};   // 前フレーム

	// マウスの状態（lX, lY, lZ, rgbButtons[8]）
	DIMOUSESTATE2 mouseState_ = {};   // 今フレーム
	DIMOUSESTATE2 mouseStatePre_ = {};   // 前フレーム
};