#include "LightComponent.h"

#include <algorithm>
#include <cmath>

#include "GameObject.h"
#include "PrimitiveDrawer.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace {
	// 度 ⇔ ラジアンの変換。このファイルの中だけで使う
	constexpr float kDegToRad = kPi / 180.0f;
	constexpr float kRadToDeg = 180.0f / kPi;
}

// ════════════════════════════════════════════════════════════════════
// LightComponent（共通部分）
// ════════════════════════════════════════════════════════════════════
Vector3 LightComponent::GetWorldPosition() const {
	//   親子関係がまだ無いので、ローカル座標＝ワールド座標
	return GetOwner()->GetTransform().translate;
}

Vector3 LightComponent::GetForward() const {
	const Transform& transform = GetOwner()->GetTransform();

	//   MakeAffineMatrix と同じ掛ける順番にすること
	//     順番が違うと、Inspector の回転とライトの向きがずれる
	const Matrix4x4 rotateMatrix =
		MakeRotateXMatrix(transform.rotate.x) *
		MakeRotateYMatrix(transform.rotate.y) *
		MakeRotateZMatrix(transform.rotate.z);

	//   ローカルの +Z を「正面」とする（Unity と同じ約束）
	return Normalize(TransformNormal(Vector3{ 0.0f, 0.0f, 1.0f }, rotateMatrix));
}

void LightComponent::DrawInspector() {
#ifdef USE_IMGUI
	ImGui::ColorEdit4("色", &color_.x);
	ImGui::DragFloat("強さ", &intensity_, 0.01f, 0.0f, 20.0f);
#endif
}

// ════════════════════════════════════════════════════════════════════
// DirectionalLightComponent
// ════════════════════════════════════════════════════════════════════
DirectionalLight DirectionalLightComponent::MakeGpuData() const {
	DirectionalLight data{};
	data.color = color_;
	data.direction = GetForward();
	data.intensity = intensity_;
	return data;
}

void DirectionalLightComponent::DrawGizmos(PrimitiveDrawer& drawer) {
	//   平行光源は位置を持たないが、置いた場所から向きを示す矢印を描く
	const Vector3 origin = GetWorldPosition();
	const Vector3 forward = GetForward();
	const Vector4 color{ 1.0f, 0.95f, 0.6f, 1.0f };

	constexpr float kLength = 2.0f;
	const Vector3 tip = origin + forward * kLength;
	drawer.DrawLine3d(origin, tip, color);

	//   矢じり。進行方向に垂直な2軸を作って、そこから4本引く
	const Vector3 helper = (std::abs(forward.y) > 0.99f)
		? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
	const Vector3 right = Normalize(Cross(helper, forward));
	const Vector3 up = Cross(forward, right);

	constexpr float kHeadSize = 0.2f;
	const Vector3 back = tip - forward * kHeadSize * 2.0f;
	drawer.DrawLine3d(tip, back + right * kHeadSize, color);
	drawer.DrawLine3d(tip, back - right * kHeadSize, color);
	drawer.DrawLine3d(tip, back + up * kHeadSize, color);
	drawer.DrawLine3d(tip, back - up * kHeadSize, color);
}

// ════════════════════════════════════════════════════════════════════
// PointLightComponent
// ════════════════════════════════════════════════════════════════════
PointLight PointLightComponent::MakeGpuData() const {
	PointLight data{};
	data.color = color_;
	data.position = GetWorldPosition();
	data.intensity = intensity_;
	data.radius = radius_;
	data.decay = decay_;
	return data;
}

void PointLightComponent::DrawInspector() {
#ifdef USE_IMGUI
	LightComponent::DrawInspector();   // 基底の色・強さを先に描く
	ImGui::DragFloat("影響半径", &radius_, 0.1f, 0.0f, 100.0f);
	ImGui::DragFloat("減衰", &decay_, 0.01f, 0.0f, 10.0f);
#endif
}

void PointLightComponent::DrawGizmos(PrimitiveDrawer& drawer) {
	const Vector3 p = GetWorldPosition();
	const Vector4 color{ 1.0f, 1.0f, 0.4f, 1.0f };

	//   位置を示す小さな十字（3軸）
	constexpr float kCrossSize = 0.3f;
	drawer.DrawLine3d({ p.x - kCrossSize, p.y, p.z }, { p.x + kCrossSize, p.y, p.z }, color);
	drawer.DrawLine3d({ p.x, p.y - kCrossSize, p.z }, { p.x, p.y + kCrossSize, p.z }, color);
	drawer.DrawLine3d({ p.x, p.y, p.z - kCrossSize }, { p.x, p.y, p.z + kCrossSize }, color);

	//   影響半径を示す円（XZ平面・XY平面の2枚）
	const Vector4 radiusColor{ 1.0f, 1.0f, 0.4f, 0.3f };
	constexpr uint32_t kSegmentCount = 32;
	for (uint32_t i = 0; i < kSegmentCount; ++i) {
		const float t0 = static_cast<float>(i) / kSegmentCount * 2.0f * kPi;
		const float t1 = static_cast<float>(i + 1) / kSegmentCount * 2.0f * kPi;

		drawer.DrawLine3d(
			{ p.x + std::cos(t0) * radius_, p.y, p.z + std::sin(t0) * radius_ },
			{ p.x + std::cos(t1) * radius_, p.y, p.z + std::sin(t1) * radius_ }, radiusColor);
		drawer.DrawLine3d(
			{ p.x + std::cos(t0) * radius_, p.y + std::sin(t0) * radius_, p.z },
			{ p.x + std::cos(t1) * radius_, p.y + std::sin(t1) * radius_, p.z }, radiusColor);
	}
}

// ════════════════════════════════════════════════════════════════════
// SpotLightComponent
// ════════════════════════════════════════════════════════════════════
void SpotLightComponent::SetAngle(float degrees) {
	angleDeg_ = std::clamp(degrees, 1.0f, 90.0f);
	//   円錐を狭めたら、減衰開始も一緒に縮める
	falloffStartDeg_ = std::min(falloffStartDeg_, angleDeg_);
}

void SpotLightComponent::SetFalloffStart(float degrees) {
	//   円錐の外には絶対に出られない
	falloffStartDeg_ = std::clamp(degrees, 0.0f, angleDeg_);
}

SpotLight SpotLightComponent::MakeGpuData() const {
	SpotLight data{};
	data.color = color_;
	data.position = GetWorldPosition();
	data.intensity = intensity_;
	data.direction = GetForward();
	data.distance = distance_;
	data.decay = decay_;
	//   度 → cos の変換はここ1箇所だけ
	data.cosAngle = std::cos(angleDeg_ * kDegToRad);
	data.cosFalloffStart = std::cos(falloffStartDeg_ * kDegToRad);
	data.padding = 0.0f;
	return data;
}

void SpotLightComponent::DrawInspector() {
#ifdef USE_IMGUI
	LightComponent::DrawInspector();
	ImGui::DragFloat("届く距離", &distance_, 0.1f, 0.0f, 100.0f);
	ImGui::DragFloat("減衰", &decay_, 0.01f, 0.0f, 10.0f);

	//   UI は素直に値を渡すだけ。関係を守るのはセッターの仕事
	float angle = angleDeg_;
	if (ImGui::DragFloat("角度(度)", &angle, 0.5f, 1.0f, 90.0f)) {
		SetAngle(angle);
	}
	float falloff = falloffStartDeg_;
	if (ImGui::DragFloat("減衰開始(度)", &falloff, 0.5f, 0.0f, 90.0f)) {
		SetFalloffStart(falloff);
	}
#endif
}

void SpotLightComponent::DrawGizmos(PrimitiveDrawer& drawer) {
	const Vector3 origin = GetWorldPosition();
	const Vector3 axis = GetForward();
	const float radius = distance_ * std::tan(angleDeg_ * kDegToRad);
	const Vector3 center = origin + axis * distance_;
	const Vector4 color{ 0.4f, 0.9f, 1.0f, 1.0f };

	//   軸に垂直な2本の基準ベクトルを作る
	//     axis とほぼ平行なベクトルを選ぶと外積が 0 になるので、Y軸かX軸を選び分ける
	const Vector3 helper = (std::abs(axis.y) > 0.99f)
		? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
	const Vector3 right = Normalize(Cross(helper, axis));
	const Vector3 up = Cross(axis, right);

	drawer.DrawLine3d(origin, center, color);

	constexpr uint32_t kSegmentCount = 24;
	Vector3 prev{};
	for (uint32_t i = 0; i <= kSegmentCount; ++i) {
		const float t = static_cast<float>(i) / kSegmentCount * 2.0f * kPi;
		const Vector3 point = center + (right * std::cos(t) + up * std::sin(t)) * radius;
		if (i > 0) {
			drawer.DrawLine3d(prev, point, color);
		}
		if (i % 6 == 0) {
			drawer.DrawLine3d(origin, point, color);
		}
		prev = point;
	}
}