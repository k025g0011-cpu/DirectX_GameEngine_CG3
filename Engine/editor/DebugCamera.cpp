#include "DebugCamera.h"
#include "Input.h"   // ここで初めて実体が必要になる
#include "MathUtility.h"   // Matrix4x4 / TransformNormal

// このcppファイルの中だけで使う定数
namespace {
	// 注視点を通り抜けて裏返るのを防ぐ最小距離
	constexpr float kMinDistance = 0.5f;
}

// ════════════════════════════════════════
// 初期化
// ════════════════════════════════════════
void DebugCamera::Initialize(Input* input, int32_t clientWidth, int32_t clientHeight) {
	input_ = input;

	// 累積回転行列は「回転なし」＝単位行列から始める
	matRot_ = MakeIdentity4x4();

	// 射影行列は画角とアスペクト比で決まり、カメラの位置・向きとは無関係
	// 毎フレーム変わらないのでここで1回だけ計算する
	projectionMatrix_ = MakePerspectiveMatrix(
		0.45f,                                                              // 垂直画角(ラジアン)
		static_cast<float>(clientWidth) / static_cast<float>(clientHeight), // アスペクト比
		0.1f,                                                               // ニアクリップ
		100.0f);                                                            // ファークリップ

	// 初期状態の行列も作っておく（最初のフレームで不定にならないように）
	// 入力処理は呼ばない。Input::Update() がまだ走っていない可能性があるため
	UpdateMatrices();
}

// ════════════════════════════════════════
// 更新
//   ① 入力によるカメラの移動や回転
//   ② ビュー行列の更新
// ════════════════════════════════════════
void DebugCamera::Update() {

	// ゲームビューの上にマウスがあるときだけ入力を処理する
	if (isInputEnabled_) {
		UpdateMouseControl();
		UpdateKeyboardMove();
	}

	// 行列は入力の有無にかかわらず毎フレーム更新する
	// （行列がゼロのままになるのを防ぐため）
	UpdateMatrices();
}

// ════════════════════════════════════════
// マウス操作
//   中ドラッグ          : 回転（オービット）
//   Shift + 中ドラッグ  : 注視点の平行移動（パン）
//   ホイール            : ズーム
// ════════════════════════════════════════
void DebugCamera::UpdateMouseControl() {

	const Vector2 mouseMove = input_->GetMouseMove();

	// ─── 中ボタン（2番）を押している間 ───
	//   Shift の有無でパンと回転を切り替える（排他）
	if (input_->PushMouseButton(2)) {

		if (IsShiftPressed()) {
			// ─── パン：注視点をカメラのローカルXY方向へ動かす ───
			// 掴んだ景色が指に付いてくる感覚にするため X は符号を反転する
			Vector3 move = { -mouseMove.x * moveSpeed_, mouseMove.y * moveSpeed_, 0.0f };
			// ローカル方向をワールド方向へ変換（平行移動成分は無視される）
			target_ += TransformNormal(move, matRot_);

		} else {
			// ─── オービット：ターンテーブル方式 ───
			const float pitch = mouseMove.y * rotateSpeed_;   // 縦ドラッグ → 見上げ／見下ろし
			const float yaw = mouseMove.x * rotateSpeed_;   // 横ドラッグ → 左右の首振り

			// ピッチはカメラのローカルX軸まわり（左に掛ける＝先に適用）
			// ヨーはワールドY軸まわり        （右に掛ける＝後に適用）
			// こうすると水平線が傾かない（Blender の既定の挙動）
			matRot_ = MakeRotateXMatrix(pitch) * matRot_ * MakeRotateYMatrix(yaw);
		}
	}

	// ─── ホイール：注視点との距離（ズーム） ───
	//   中ボタンの if の外。ドラッグ中でもホイールは効かせたい
	const float wheel = input_->GetMouseWheel();
	if (wheel != 0.0f) {
		distance_ -= wheel * wheelSpeed_;   // 奥に回す＝近づく
		// 注視点を通り抜けて裏返るのを防ぐ
		if (distance_ < kMinDistance) { distance_ = kMinDistance; }
	}
}

// ════════════════════════════════════════
// キーボードによる注視点の移動
//   6方向を1つのベクトルに足し込む（斜め移動が自動で手に入る）
// ════════════════════════════════════════
void DebugCamera::UpdateKeyboardMove() {

	Vector3 keyMove = { 0.0f, 0.0f, 0.0f };
	if (input_->PushKey(DIK_W)) { keyMove.z += keySpeed_; }   // 前
	if (input_->PushKey(DIK_S)) { keyMove.z -= keySpeed_; }   // 後
	if (input_->PushKey(DIK_D)) { keyMove.x += keySpeed_; }   // 右
	if (input_->PushKey(DIK_A)) { keyMove.x -= keySpeed_; }   // 左
	if (input_->PushKey(DIK_E)) { keyMove.y += keySpeed_; }   // 上
	if (input_->PushKey(DIK_Q)) { keyMove.y -= keySpeed_; }   // 下

	// 角度は毎フレーム変わるので、移動ベクトルは毎回変換し直す（キャッシュ不可）
	target_ += TransformNormal(keyMove, matRot_);
}

// ════════════════════════════════════════
// 行列の更新
//   入力を一切見ない。状態（matRot_ / target_ / distance_）から
//   行列を求めるだけの関数にしておく
// ════════════════════════════════════════
void DebugCamera::UpdateMatrices() {

	// ピボット回転：注視点から distance_ だけ後ろへ下がった位置がカメラの座標
	// ローカルの {0, 0, -distance_} を現在の姿勢で回せば、自動的に円軌道になる
	const Vector3 offset = TransformNormal({ 0.0f, 0.0f, -distance_ }, matRot_);
	translation_ = target_ + offset;

	// 座標から平行移動行列を計算する
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translation_);

	// 累積回転行列と平行移動行列からワールド行列を計算する
	// 「原点で回してから目的地へ運ぶ」順序。逆にすると原点の周りを公転してしまう
	Matrix4x4 worldMatrix = matRot_ * translateMatrix;

	// ワールド行列の逆行列をビュー行列に代入する
	viewMatrix_ = Inverse(worldMatrix);

	// ビュー×射影を合成しておく（Camera と同じ窓口を持たせる）
	viewProjectionMatrix_ = viewMatrix_ * projectionMatrix_;
}

// ════════════════════════════════════════
// 修飾キーの判定
//   DirectInput は左右の Shift / Ctrl を別のキーとして扱う。
//   片方しか見ないと、右側のキーを使う人には効かない
// ════════════════════════════════════════
bool DebugCamera::IsShiftPressed() const {
	return input_->PushKey(DIK_LSHIFT) || input_->PushKey(DIK_RSHIFT);
}
