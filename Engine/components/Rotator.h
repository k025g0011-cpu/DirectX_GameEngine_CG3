#pragma once

#include "Component.h"
#include "MathUtility.h"

// ════════════════════════════════════════════════════════════════════
// 一定速度で回り続けるコンポーネント
//   GameObject の Transform.rotate を毎フレーム加算するだけ
//   ・軸ごとに速度を持つので、斜め回転もできる
//   ・単位はラジアン/秒（経過秒を掛けるので FPS に依存しない）
// ════════════════════════════════════════════════════════════════════
class Rotator : public Component {
public:
	/// <summary>回転速度を指定して作る</summary>
	/// <param name="rotationSpeed">毎秒の回転量（ラジアン）。既定は Y軸に毎秒1ラジアン</param>
	explicit Rotator(const Vector3& rotationSpeed = { 0.0f, 1.0f, 0.0f })
		: rotationSpeed_(rotationSpeed) {}

	// ───── Component の実装 ─────
	void Update() override;
	void DrawInspector() override;
	const char* GetTypeName() const override { return "Rotator"; }

	// ───── 自分固有の機能 ─────
	const Vector3& GetRotationSpeed() const { return rotationSpeed_; }
	void SetRotationSpeed(const Vector3& rotationSpeed) { rotationSpeed_ = rotationSpeed; }

private:
	// 毎秒の回転量（ラジアン）
	Vector3 rotationSpeed_{ 0.0f, 1.0f, 0.0f };
};