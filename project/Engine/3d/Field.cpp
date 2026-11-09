#include "Field.h"

#include <array>
#include <utility>   // std::swap

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
// min と max が逆転していたら入れ替える
//   min > max の箱は、Contains が必ず false を返す
//   症状は「場が効かない」なので、原因が範囲の反転だと気づきにくい
//   インスペクタで触れる値なので、入力の直後に直しておく
// ════════════════════════════════════════════════════════════════════
void AABB::Normalize() {
	if (min.x > max.x) { std::swap(min.x, max.x); }
	if (min.y > max.y) { std::swap(min.y, max.y); }
	if (min.z > max.z) { std::swap(min.z, max.z); }
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
// 効果範囲の「形」を線で描く
//   有効／無効で色を変える。無効でも位置は見えるようにしておく
//   描くかどうかの判断は、非 virtual の Field::DrawGizmos が済ませている
// ════════════════════════════════════════════════════════════════════
void Field::DrawAreaGizmos(PrimitiveDrawer& drawer) const {

	const Vector4 color = isEnabled_
		? Vector4{ 0.3f, 0.8f, 1.0f, 1.0f }     // 有効：水色
	: Vector4{ 0.3f, 0.35f, 0.4f, 1.0f };   // 無効：くすんだ灰青

	DrawAABBWireframe(drawer, area_, color);
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
//   どの場にも共通の設定をここで描き、最後に派生固有の設定を描かせる
//   呼ぶ側はこの1つを呼べばよい
// ════════════════════════════════════════════════════════════════════
void Field::DrawInspector() {
#ifdef USE_IMGUI

	//   「有効」は呼ぶ側がヘッダ行に置く（折りたたんだままでも押せるように）
	//   ここには、開いたときだけ見えればよい設定を並べる
	ImGui::Checkbox("ギズモ", &isGizmoVisible_);
	ImGui::SameLine();
	ImGui::Checkbox("範囲で絞る", &isAreaLimited_);

	//   範囲で絞らないときは AABB をいじっても意味がないので、触れなくする
	//   BeginDisabled と EndDisabled は、必ず対で呼ぶ
	ImGui::BeginDisabled(!isAreaLimited_);
	{
		ImGui::DragFloat3("範囲 min", &area_.min.x, 0.1f);
		ImGui::DragFloat3("範囲 max", &area_.max.x, 0.1f);

		//   ドラッグ中に min が max を追い越すことがある
		area_.Normalize();
	}
	ImGui::EndDisabled();

	ImGui::Separator();

	//   ここから下は派生が描く
	DrawOwnInspector();

#endif
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
// インスペクタ（この場だけの設定）
//   有効・範囲・ギズモは基底が描くので、ここでは加速度だけ
// ════════════════════════════════════════════════════════════════════
void AccelerationField::DrawOwnInspector() {
#ifdef USE_IMGUI
	ImGui::DragFloat3("加速度", &acceleration_.x, 0.1f);
#endif
}
