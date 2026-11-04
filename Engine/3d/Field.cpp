#include "Field.h"

#include <array>

#include "Particle.h"
#include "PrimitiveDrawer.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// このcppファイルの中だけで使う定義
namespace {

	/// <summary>AABB の辺1本（頂点番号のペア）</summary>
	struct BoxEdge {
		uint32_t a;
		uint32_t b;
	};

	// 直方体の12本の辺
	//   頂点の番号は DrawAABBWireframe の corners に対応している
	constexpr std::array<BoxEdge, 12> kBoxEdges = { {
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },   // 手前の面（min.z 側）
		{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },   // 奥の面（max.z 側）
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },   // 手前と奥をつなぐ4本
	} };

}   // namespace

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
// AABB をワイヤーフレームで描く
//   8頂点を作ってから、辺の表を回して12本の線を積む
// ════════════════════════════════════════════════════════════════════
void DrawAABBWireframe(PrimitiveDrawer& drawer, const AABB& area, const Vector4& color) {

	// 8つの角。番号は kBoxEdges と対応させること
	//
	//       3 ───── 2        y
	//      /|      /|        |
	//     7 ───── 6 |        └── x
	//     | 0 ────|─1       /
	//     |/      |/       z
	//     4 ───── 5
	const std::array<Vector3, 8> corners = { {
		{ area.min.x, area.min.y, area.min.z },   // 0
		{ area.max.x, area.min.y, area.min.z },   // 1
		{ area.max.x, area.max.y, area.min.z },   // 2
		{ area.min.x, area.max.y, area.min.z },   // 3
		{ area.min.x, area.min.y, area.max.z },   // 4
		{ area.max.x, area.min.y, area.max.z },   // 5
		{ area.max.x, area.max.y, area.max.z },   // 6
		{ area.min.x, area.max.y, area.max.z },   // 7
	} };

	for (const BoxEdge& edge : kBoxEdges) {
		drawer.DrawLine3d(corners[edge.a], corners[edge.b], color);
	}
}

// ════════════════════════════════════════════════════════════════════
// 効果範囲を線で描く
//   有効／無効で色を変える。無効でも位置は見えるようにしておく
// ════════════════════════════════════════════════════════════════════
void Field::DrawGizmos(PrimitiveDrawer& drawer) const {

	const Vector4 color = isEnabled_
		? Vector4{ 0.3f, 0.8f, 1.0f, 1.0f }     // 有効：水色
	: Vector4{ 0.3f, 0.35f, 0.4f, 1.0f };   // 無効：くすんだ灰青

	DrawAABBWireframe(drawer, area_, color);
}

// ════════════════════════════════════════════════════════════════════
// 加速度を与える
//   加速度 → 速度 → 位置 の2段階。位置は ParticleSystem が進める
// ════════════════════════════════════════════════════════════════════
void AccelerationField::Apply(Particle& particle, float deltaTime) const {

	// 加速度は「1秒あたりの速度の変化量」なので、経過秒数を掛ける
	particle.velocity += acceleration_ * deltaTime;
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
