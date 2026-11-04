#pragma once

#include "Component.h"
#include "MathUtility.h"
#include "Object3dStructs.h"

// ════════════════════════════════════════════════════════════════════
// 光源コンポーネントの共通部分
//   色と強さ、そして「Transform から位置と向きを取る」手段を持つ
//   GPU へ渡す構造体の形は派生クラスが決める
// ════════════════════════════════════════════════════════════════════
class LightComponent : public Component {
public:
	void DrawInspector() override;

	const Vector4& GetColor() const { return color_; }
	float GetIntensity() const { return intensity_; }

protected:
	LightComponent() = default;

	/// <summary>持ち主のワールド座標</summary>
	Vector3 GetWorldPosition() const;

	/// <summary>持ち主の向いている方向（ローカル +Z）</summary>
	Vector3 GetForward() const;

	Vector4 color_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float intensity_ = 1.0f;
};

// ════════════════════════════════════════════════════════════════════
// 平行光源。位置は無視され、向きだけが使われる
// ════════════════════════════════════════════════════════════════════
class DirectionalLightComponent : public LightComponent {
public:
	const char* GetTypeName() const override { return "DirectionalLight"; }
	void DrawGizmos(PrimitiveDrawer& drawer) override;

	/// <summary>GPU へ渡す構造体を組み立てる</summary>
	DirectionalLight MakeGpuData() const;
};

// ════════════════════════════════════════════════════════════════════
// 点光源。位置だけが使われる
// ════════════════════════════════════════════════════════════════════
class PointLightComponent : public LightComponent {
public:
	const char* GetTypeName() const override { return "PointLight"; }
	void DrawInspector() override;
	void DrawGizmos(PrimitiveDrawer& drawer) override;

	PointLight MakeGpuData() const;

private:
	float radius_ = 10.0f;
	float decay_ = 1.0f;
};

// ════════════════════════════════════════════════════════════════════
// スポットライト。位置と向きの両方を使う
//   角度は「度」で持ち、GPU へ渡すときだけ cos に変える
//   「減衰開始 <= 角度」という関係は、このクラスが責任を持って守る
// ════════════════════════════════════════════════════════════════════
class SpotLightComponent : public LightComponent {
public:
	const char* GetTypeName() const override { return "SpotLight"; }
	void DrawInspector() override;
	void DrawGizmos(PrimitiveDrawer& drawer) override;

	SpotLight MakeGpuData() const;

	/// <summary>円錐の半角（度）。減衰開始が外に出ないよう一緒に縮める</summary>
	void SetAngle(float degrees);
	/// <summary>減衰が始まる角度（度）。円錐の角度を超えない</summary>
	void SetFalloffStart(float degrees);

private:
	float distance_ = 7.0f;
	float decay_ = 2.0f;
	float angleDeg_ = 60.0f;
	float falloffStartDeg_ = 30.0f;
};

