#include "Rotator.h"

#include "EngineContext.h"
#include "GameObject.h"
#include "GameTime.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// ════════════════════════════════════════════════════════════════════
// 更新
//   GameObject::Update() から毎フレーム呼ばれる
// ════════════════════════════════════════════════════════════════════
void Rotator::Update() {

	// 経過秒は自分で取りに行く（シーンから渡してもらう必要がない）
	const float deltaTime = GetContext().gameTime->GetDeltaTime();

	// 持ち主の Transform を「参照」で受け取る
	//   & を付けないとコピーになり、書き換えても元に反映されない
	Transform& transform = GetOwner()->GetTransform();

	transform.rotate.x += rotationSpeed_.x * deltaTime;
	transform.rotate.y += rotationSpeed_.y * deltaTime;
	transform.rotate.z += rotationSpeed_.z * deltaTime;
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
// ════════════════════════════════════════════════════════════════════
void Rotator::DrawInspector() {
#ifdef USE_IMGUI

	ImGui::DragFloat3("速度 (rad/s)", &rotationSpeed_.x, 0.01f);

	// 度数法でも見えるようにしておく（ラジアンは直感的でないため）
	ImGui::Text("= %.0f, %.0f, %.0f deg/s",
		rotationSpeed_.x * kRadToDeg,
		rotationSpeed_.y * kRadToDeg,
		rotationSpeed_.z * kRadToDeg);

	if (ImGui::Button("停止")) {
		rotationSpeed_ = { 0.0f, 0.0f, 0.0f };
	}

#endif
}