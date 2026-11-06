#include "Input.h"

#include <cassert>
#include <cstring>

// このcppで使うライブラリはこのcppで指定する
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

// ════════════════════════════════════════
// 初期化
//   ① 窓口を作る → ② デバイスを取り出す → ③ データ形式 → ④ 共有ルール
//   ②〜④をキーボードとマウスの2回繰り返す。窓口(①)は共用
// ════════════════════════════════════════
void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {
	HRESULT hr{};

	// ─── ① DirectInputオブジェクトの生成 ───
	hr = DirectInput8Create(
		hInstance,                                             // このアプリのインスタンスハンドル
		DIRECTINPUT_VERSION,                                   // 使用するDirectInputのバージョン
		IID_IDirectInput8,                                     // 欲しいインターフェースのGUID
		reinterpret_cast<void**>(directInput_.GetAddressOf()), // ComPtrが持つポインタのアドレス
		nullptr);                                              // COMアグリゲーション用。常にnullptr
	assert(SUCCEEDED(hr));

	// ─── ② キーボード ───
	hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
	assert(SUCCEEDED(hr));

	// c_dfDIKeyboard : BYTE[256] で受け取る形式
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	// FOREGROUND   : 手前にあるときだけ受け付ける
	// NONEXCLUSIVE : 独占しない（ImGuiやVSにも入力が届く）
	// NOWINKEY     : Windowsキーを無効化
	hr = keyboard_->SetCooperativeLevel(
		hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	// ─── ③ マウス ───
	// 窓口(directInput_)はキーボードと共用。GUIDとデータ形式だけが違う
	hr = directInput_->CreateDevice(GUID_SysMouse, &mouseDevice_, NULL);
	assert(SUCCEEDED(hr));

	// c_dfDIMouse2 : DIMOUSESTATE2 で受け取る形式（ボタン8個版）
	hr = mouseDevice_->SetDataFormat(&c_dfDIMouse2);
	assert(SUCCEEDED(hr));

	// マウスにWindowsキーは無いので NOWINKEY は付けない
	hr = mouseDevice_->SetCooperativeLevel(
		hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));
}

// ════════════════════════════════════════
// 毎フレームの更新
//   「前フレームへ退避 → Acquire → 取得（失敗時はゼロクリア）」を2デバイス分
// ════════════════════════════════════════
void Input::Update() {
	// ─── キーボード ───
	std::memcpy(keyPre_, key_, sizeof(key_));   // 退避は取得より前に行う

	// 非アクティブ時は失敗するのが正常なので戻り値は見ない
	// （アクティブ時は「獲得済み」を意味する S_FALSE が返り続ける）
	keyboard_->Acquire();

	// 失敗時はバッファが更新されないため明示的に全キーOFFへ倒す
	// （押しながらAlt+Tabした際のキー固着を防ぐ）
	if (FAILED(keyboard_->GetDeviceState(sizeof(key_), key_))) {
		std::memset(key_, 0, sizeof(key_));
	}

	// ─── マウス ───
	mouseStatePre_ = mouseState_;   // 構造体は代入でコピーできる

	mouseDevice_->Acquire();

	if (FAILED(mouseDevice_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_))) {
		mouseState_ = {};   // 移動量もボタンも全部0
	}
}

// ════════════════════════════════════════
// キーボード判定
// ════════════════════════════════════════
bool Input::PushKey(uint8_t keyNumber) const {
	return key_[keyNumber] != 0;
}

bool Input::ReleaseKey(uint8_t keyNumber) const {
	return key_[keyNumber] == 0;
}

bool Input::TriggerKey(uint8_t keyNumber) const {
	return key_[keyNumber] != 0 && keyPre_[keyNumber] == 0;
}

bool Input::ReleaseTriggerKey(uint8_t keyNumber) const {
	return key_[keyNumber] == 0 && keyPre_[keyNumber] != 0;
}

// ════════════════════════════════════════
// マウスボタン判定
//   int32_t なので範囲外が書けてしまう。assertで防ぐ
// ════════════════════════════════════════
bool Input::PushMouseButton(int32_t buttonNumber) const {
	assert(0 <= buttonNumber && buttonNumber < 8);
	return mouseState_.rgbButtons[buttonNumber] != 0;
}

bool Input::ReleaseMouseButton(int32_t buttonNumber) const {
	assert(0 <= buttonNumber && buttonNumber < 8);
	return mouseState_.rgbButtons[buttonNumber] == 0;
}

bool Input::TriggerMouseButton(int32_t buttonNumber) const {
	assert(0 <= buttonNumber && buttonNumber < 8);
	return mouseState_.rgbButtons[buttonNumber] != 0
		&& mouseStatePre_.rgbButtons[buttonNumber] == 0;
}

bool Input::ReleaseTriggerMouseButton(int32_t buttonNumber) const {
	assert(0 <= buttonNumber && buttonNumber < 8);
	return mouseState_.rgbButtons[buttonNumber] == 0
		&& mouseStatePre_.rgbButtons[buttonNumber] != 0;
}

// ════════════════════════════════════════
// マウスの移動量
// ════════════════════════════════════════
Vector2 Input::GetMouseMove() const {
	// lX / lY は LONG（整数）。計算しやすいよう float に変換して返す
	return Vector2{
		static_cast<float>(mouseState_.lX),
		static_cast<float>(mouseState_.lY)
	};
}

float Input::GetMouseWheel() const {
	// lZ は 1ノッチで ±120（WHEEL_DELTA）。ここで正規化して「ノッチ数」で返す
	// 生の120を外へ出すと、使う側が毎回120で割る羽目になる
	return static_cast<float>(mouseState_.lZ) / static_cast<float>(WHEEL_DELTA);
}