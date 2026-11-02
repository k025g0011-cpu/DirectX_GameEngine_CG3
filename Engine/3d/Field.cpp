#include "Field.h"

#include "Particle.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// ════════════════════════════════════════════════════════════════════
// 点が箱の中にあるか
//   3軸すべてで「min 以上 max 以下」なら中にいる
// ════════════════════════════════════════════════════════════════════
bool AABB::Contains(const Vector3& point) const {
	return min.x <= point.x && point.x <= max.x
		&& min.y <= point.y && point.y <= max.y
		&& min.z <= point.z && point.z <= max.z;
}

// ════════════════════════════════════════════════════════════════════
// 加速度を与える
//   加速度 → 速度 → 位置 の2段階。位置は ParticleSystem が進める
// ════════════════════════════════════════════════════════════════════
void AccelerationField::Apply(Particle& particle, float deltaTime) const {

	// 加速度は「1秒あたりの速度の変化量」なので、経過秒数を掛ける
	particle.velocity.x += acceleration_.x * deltaTime;
	particle.velocity.y += acceleration_.y * deltaTime;
	particle.velocity.z += acceleration_.z * deltaTime;
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
//   範囲（AABB）は基底が描くので、ここでは加速度だけ
// ════════════════════════════════════════════════════════════════════
void AccelerationField::DrawInspector() {
#ifdef USE_IMGUI
	ImGui::DragFloat3("加速度", &acceleration_.x, 0.1f);
#endif
}
