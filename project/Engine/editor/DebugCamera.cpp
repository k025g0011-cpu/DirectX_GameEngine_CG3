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

	// 「回転なし」から始める
	//   Quaternion の既定値も単位だが、再初期化できるよう明示的に入れる
	rotation_ = MakeIdentityQuaternion();

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
void DebugCamera::Update(float deltaTime) {

	// ゲームビューの上にマウスがあるときだけ入力を処理する
	if (isInputEnabled_) {
		UpdateMouseControl();
		UpdateKeyboardMove(deltaTime);
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
			// ローカル方向をワールド方向へ変換（行列を作らずに回せる）
			target_ += RotateVector(move, rotation_);

		} else {
			// ─── オービット：ターンテーブル方式 ───
			const float pitch = mouseMove.y * rotateSpeed_;   // 縦ドラッグ → 見上げ／見下ろし
			const float yaw = mouseMove.x * rotateSpeed_;   // 横ドラッグ → 左右の首振り

			// ピッチはカメラのローカルX軸まわり（左に掛ける＝先に適用）
			// ヨーはワールドY軸まわり        （右に掛ける＝後に適用）
			// こうすると水平線が傾かない（Blender の既定の挙動）
			rotation_ = MakeRotateAxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, pitch)
				* rotation_
				* MakeRotateAxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, yaw);

			// 毎フレーム掛け算するので、誤差がたまる前に長さを 1 に戻す
			//   行列のままだと、ここで3本の軸を直交に戻す処理が必要だった
			rotation_ = Normalize(rotation_);
		}
	}

	// ─── ホイール：注視点との距離（ズーム） ───
	//   中ボタンの if の外。ドラッグ中でもホイールは効かせたい
	const float wheel = input_->GetMouseWheel();
	if (wheel != 0.0f) {
		// 下限の判定は SetDistance が持っているので、ここでは計算だけ
		SetDistance(distance_ - wheel * wheelSpeed_);   // 下限を通す
	}
}

// ════════════════════════════════════════
// キーボードによる注視点の移動
//   6方向を1つのベクトルに足し込む（斜め移動が自動で手に入る）
// ════════════════════════════════════════
void DebugCamera::UpdateKeyboardMove(float deltaTime) {

	// 秒速に経過秒数を掛けて、このフレームの移動量にする
	const float speed = keySpeed_ * deltaTime;

	Vector3 keyMove = { 0.0f, 0.0f, 0.0f };
	if (input_->PushKey(DIK_W)) { keyMove.z += speed; }   // 前
	if (input_->PushKey(DIK_S)) { keyMove.z -= speed; }   // 後
	if (input_->PushKey(DIK_D)) { keyMove.x += speed; }   // 右
	if (input_->PushKey(DIK_A)) { keyMove.x -= speed; }   // 左
	if (input_->PushKey(DIK_E)) { keyMove.y += speed; }   // 上
	if (input_->PushKey(DIK_Q)) { keyMove.y -= speed; }   // 下

	// 角度は毎フレーム変わるので、移動ベクトルは毎回変換し直す（キャッシュ不可）
	target_ += RotateVector(keyMove, rotation_);
}

// ════════════════════════════════════════
// 行列の更新
//   入力を一切見ない。状態（matRot_ / target_ / distance_）から
//   行列を求めるだけの関数にしておく
// ════════════════════════════════════════
void DebugCamera::UpdateMatrices() {

	// ピボット回転：注視点から distance_ だけ後ろへ下がった位置がカメラの座標
	// ローカルの {0, 0, -distance_} を現在の姿勢で回せば、自動的に円軌道になる
	const Vector3 offset = RotateVector({ 0.0f, 0.0f, -distance_ }, rotation_);
	worldPosition_ = target_ + offset;

	// 向きと位置からワールド行列を作る
	// 「原点で回してから目的地へ運ぶ」順序。逆にすると原点の周りを公転してしまう
	const Matrix4x4 worldMatrix =
		MakeRotateMatrix(rotation_) * MakeTranslateMatrix(worldPosition_);

	// ワールド行列の逆行列をビュー行列に代入する
	viewMatrix_ = Inverse(worldMatrix);

	// ビュー×射影を合成しておく（Camera と同じ窓口を持たせる）
	viewProjectionMatrix_ = viewMatrix_ * projectionMatrix_;
}

// ════════════════════════════════════════
// 注視点からの距離
//   ホイール操作もここを通すので、下限の判定が1箇所に集まる
// ════════════════════════════════════════
void DebugCamera::SetDistance(float distance) {
	distance_ = (distance < kMinDistance) ? kMinDistance : distance;
}

// ════════════════════════════════════════
// 修飾キーの判定
//   DirectInput は左右の Shift / Ctrl を別のキーとして扱う。
//   片方しか見ないと、右側のキーを使う人には効かない
// ════════════════════════════════════════
bool DebugCamera::IsShiftPressed() const {
	return input_->PushKey(DIK_LSHIFT) || input_->PushKey(DIK_RSHIFT);
}
