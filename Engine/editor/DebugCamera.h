#pragma once

#include <cstdint>

#include "MathUtility.h"

// 前方宣言：ポインタとして使うだけなので #include は不要
class Input;

// ════════════════════════════════════════
// デバッグカメラ
//   開発中にシーンを自由な視点から確認するためのカメラ
//   回転は累積回転行列で管理する（オイラー角のジンバルロック回避）
//   注視点(target_)を中心に回り込むピボット回転
//
//   左ドラッグ   : 回転（回り込み）
//   中ドラッグ   : 注視点の平行移動
//   ホイール     : 注視点との距離（ズーム）
//   WASD / QE    : 注視点の前後左右上下移動
// ════════════════════════════════════════
class DebugCamera {
public:
	/// <summary>初期化</summary>
	/// <param name="input">入力クラスへのポインタ。所有はしない</param>
	/// <param name="clientWidth">クライアント領域の横幅（アスペクト比の計算用）</param>
	/// <param name="clientHeight">クライアント領域の縦幅</param>
	void Initialize(Input* input, int32_t clientWidth, int32_t clientHeight);

	/// <summary>更新。毎フレーム呼ぶ</summary>
	void Update();

	// ───── ゲッター ─────
	// 短く中身が変わらないものはヘッダに直接書いてよい（暗黙にinline扱い）
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
	/// <summary>ビュー行列 × 射影行列。描画時はこれを渡す</summary>
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }  

	// ImGui表示用
	const Vector3& GetTranslation() const { return translation_; }
	const Vector3& GetTarget() const { return target_; }
	float GetDistance() const { return distance_; }

	/// <summary>入力を受け付けるかどうか（false でも行列の更新は行う）</summary>
	void SetInputEnabled(bool enabled) { isInputEnabled_ = enabled; }
	bool IsInputEnabled() const { return isInputEnabled_; }

private:
	// 借り物のポインタ。deleteしない
	Input* input_ = nullptr;

	// ───── カメラの状態（これが本体） ─────
	// 累積回転行列（オイラー角 rotation_ は廃止）
	Matrix4x4 matRot_{};
	// ピボットポイント（回転と移動の中心）
	Vector3 target_ = { 0.0f, 1.0f, 0.0f };
	// 注視点からカメラまでの距離
	float distance_ = 5.0f;

	// ───── 計算結果 ─────
	// カメラのワールド座標。target_ と distance_ と matRot_ から毎フレーム求まる
	Vector3 translation_{};
	Matrix4x4 viewMatrix_{};
	Matrix4x4 projectionMatrix_{};
	Matrix4x4 viewProjectionMatrix_{};  

	// ───── 操作感度 ─────
	float rotateSpeed_ = 0.005f;   // マウス1ピクセルあたりのラジアン
	float moveSpeed_ = 0.005f;   // マウス1ピクセルあたりのワールド単位
	float wheelSpeed_ = 0.5f;     // ホイール1ノッチあたりのワールド単位
	float keySpeed_ = 0.05f;    // キー1フレームあたりのワールド単位

	// ───── 入力受付フラグ ─────
	bool isInputEnabled_ = true;
};