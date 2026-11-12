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

	//   このフレームぶんの回転を1つ作る
	const Quaternion deltaRotation = MakeRotateXYZQuaternion(rotationSpeed_ * deltaTime);

	// ────────────────────────────────────────────────────────────────
	// 掛ける左右で「どの軸で回るか」が決まる
	//   左が先に適用されるので
	//     delta * current … delta が先に効く ＝ 自分の軸（ローカル）
	//     current * delta … delta が後に効く ＝ ワールドの軸
	// ────────────────────────────────────────────────────────────────
	transform.rotate = useLocalAxis_
		? deltaRotation * transform.rotate
		: transform.rotate * deltaRotation;

	//   毎フレーム掛け算するので、誤差がたまる前に長さを 1 に戻す
	transform.rotate = Normalize(transform.rotate);
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

	ImGui::Checkbox("自分の軸で回す", &useLocalAxis_);

	if (ImGui::Button("停止")) {
		rotationSpeed_ = { 0.0f, 0.0f, 0.0f };
	}

#endif
}