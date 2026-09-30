#pragma once

#include "MathUtility.h"

// ════════════════════════════════════════════════════════════════════
// 3D カメラ
//   位置と向きから ビュー行列 を、画角から 射影行列 を作る
//
//   ビュー行列 ＝ カメラのワールド行列の逆行列
//     「カメラを動かす」のではなく「世界をカメラの逆向きに動かす」と考える
// ════════════════════════════════════════════════════════════════════
class Camera {
public:
	/// <summary>初期化。既定値で行列を1回作る</summary>
	void Initialize();

	/// <summary>行列を作り直す。値を変えたあと、描画前に呼ぶ</summary>
	void Update();

	// ────────────────────────────────────────────────────────────────
	// 位置と向き
	// ────────────────────────────────────────────────────────────────
	const Vector3& GetTranslate() const { return translate_; }
	void SetTranslate(const Vector3& translate) { translate_ = translate; }

	/// <summary>向き（ラジアン）。XYZ軸まわりの回転</summary>
	const Vector3& GetRotate() const { return rotate_; }
	void SetRotate(const Vector3& rotate) { rotate_ = rotate; }

	// ────────────────────────────────────────────────────────────────
	// 射影（どう映すか）
	// ────────────────────────────────────────────────────────────────
	float GetFovY() const { return fovY_; }
	void SetFovY(float fovY) { fovY_ = fovY; }                 // 縦方向の画角（ラジアン）

	float GetAspectRatio() const { return aspectRatio_; }
	void SetAspectRatio(float aspectRatio) { aspectRatio_ = aspectRatio; }

	float GetNearClip() const { return nearClip_; }
	void SetNearClip(float nearClip) { nearClip_ = nearClip; } // これより近いものは映さない

	float GetFarClip() const { return farClip_; }
	void SetFarClip(float farClip) { farClip_ = farClip; }     // これより遠いものは映さない

	// ────────────────────────────────────────────────────────────────
	// 行列の取得（Update で作られたもの）
	// ────────────────────────────────────────────────────────────────
	const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }

	/// <summary>ビュー行列 × 射影行列。描画時はこれを渡す</summary>
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }

private:
	// ───── カメラの姿勢 ─────
	Vector3 rotate_{ 0.0f, 0.0f, 0.0f };
	Vector3 translate_{ 0.0f, 0.0f, -5.0f };   // z = -5 から原点方向を見る

	// ───── 射影のパラメータ ─────
	float fovY_ = 0.45f;          // 縦方向の画角（約26度）
	float aspectRatio_ = 16.0f / 9.0f;
	float nearClip_ = 0.1f;
	float farClip_ = 100.0f;

	// ───── Update で作られる行列 ─────
	Matrix4x4 worldMatrix_{};
	Matrix4x4 viewMatrix_{};
	Matrix4x4 projectionMatrix_{};
	Matrix4x4 viewProjectionMatrix_{};
};