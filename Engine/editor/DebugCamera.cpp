#include "DebugCamera.h"

#include "Input.h"   // ここで初めて実体が必要になる

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

	// 初期状態のビュー行列も作っておく（最初のフレームで不定にならないように）
	Update();
}

// ════════════════════════════════════════
// 更新
//   ① 入力によるカメラの移動や回転
//   ② ビュー行列の更新
// ════════════════════════════════════════
void DebugCamera::Update() {

	// ────────────────────────────────────────
	// ① 入力によるカメラの移動や回転
	//    ゲームビューの上にマウスがあるときだけ処理する
	// ────────────────────────────────────────
	if (isInputEnabled_) {

		Vector2 mouseMove = input_->GetMouseMove();

		// ─── 左ドラッグ：回転（累積回転行列に合成） ───
		if (input_->PushMouseButton(0)) {
			// 今回フレームでの追加回転角（累積した角度ではなく差分）
			float deltaX = mouseMove.y * rotateSpeed_;   // 縦ドラッグ → 見上げ／見下ろし = 回転軸はX
			float deltaY = mouseMove.x * rotateSpeed_;   // 横ドラッグ → 左右の首振り     = 回転軸はY

			// 追加回転分の回転行列を生成（毎フレームの回転量行列をここで作るmatRotDelta）
			Matrix4x4 matRotDelta = MakeIdentity4x4();
			matRotDelta = matRotDelta * MakeRotateXMatrix(deltaX); // 回転軸はXなのでXRotate関数
			matRotDelta = matRotDelta * MakeRotateYMatrix(deltaY); // 回転軸はXなのでYRotate関数

			// 累積の回転行列を合成
			// Delta を左に置く＝先に適用＝カメラのローカル軸で回る（順序を逆にすると破綻する）
			matRot_ = matRotDelta * matRot_;
		}

		// ─── 中ドラッグ：注視点の平行移動（カメラのローカルXY方向へ） ───
		if (input_->PushMouseButton(2)) {
			// 掴んだ景色が指に付いてくる感覚にするため X は符号を反転する
			Vector3 move = { -mouseMove.x * moveSpeed_, mouseMove.y * moveSpeed_, 0.0f };
			// ローカル方向をワールド方向へ変換（平行移動成分は無視される）
			move = TransformNormal(move, matRot_);
			target_.x += move.x;
			target_.y += move.y;
			target_.z += move.z;
		}

		// ─── キーボード：注視点の前後左右上下移動 ───
		// 6方向を1つのベクトルに足し込む（斜め移動が自動で手に入る）
		Vector3 keyMove = { 0.0f, 0.0f, 0.0f };
		if (input_->PushKey(DIK_W)) { keyMove.z += keySpeed_; }   // 前
		if (input_->PushKey(DIK_S)) { keyMove.z -= keySpeed_; }   // 後
		if (input_->PushKey(DIK_D)) { keyMove.x += keySpeed_; }   // 右
		if (input_->PushKey(DIK_A)) { keyMove.x -= keySpeed_; }   // 左
		if (input_->PushKey(DIK_E)) { keyMove.y += keySpeed_; }   // 上
		if (input_->PushKey(DIK_Q)) { keyMove.y -= keySpeed_; }   // 下

		// 角度は毎フレーム変わるので、移動ベクトルは毎回変換し直す（キャッシュ不可）
		keyMove = TransformNormal(keyMove, matRot_);
		target_.x += keyMove.x;
		target_.y += keyMove.y;
		target_.z += keyMove.z;

		// ─── ホイール：注視点との距離（ズーム） ───
		float wheel = input_->GetMouseWheel();
		if (wheel != 0.0f) {
			distance_ -= wheel * wheelSpeed_;   // 奥に回す＝近づく
			// 注視点を通り抜けて裏返るのを防ぐ
			const float kMinDistance = 0.5f;
			if (distance_ < kMinDistance) { distance_ = kMinDistance; }
		}

	} 

	// ────────────────────────────────────────
	// ② ビュー行列の更新
	//    入力の有無にかかわらず毎フレーム行う
	//    （行列がゼロのままになるのを防ぐため）
	// ────────────────────────────────────────

	// ピボット回転：注視点から distance_ だけ後ろへ下がった位置がカメラの座標
	// ローカルの {0, 0, -distance_} を現在の姿勢で回せば、自動的に円軌道になる
	Vector3 offset = TransformNormal({ 0.0f, 0.0f, -distance_ }, matRot_);
	translation_.x = target_.x + offset.x;
	translation_.y = target_.y + offset.y;
	translation_.z = target_.z + offset.z;

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