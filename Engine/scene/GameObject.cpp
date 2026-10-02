#include "GameObject.h"

#include "EngineContext.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void GameObject::Initialize(const EngineContext& context, const std::string& name) {
	context_ = &context;
	name_ = name;
}

// ════════════════════════════════════════════════════════════════════
// 更新
//   自分では何もせず、コンポーネントに配るだけ
// ════════════════════════════════════════════════════════════════════
void GameObject::Update() {
	if (!isActive_) {
		return;
	}

	for (const std::unique_ptr<Component>& component : components_) {
		if (component->IsEnabled()) {
			component->Update();
		}
	}
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   PreDraw と PostDraw の間で呼ばれる。値は変えない
// ════════════════════════════════════════════════════════════════════
void GameObject::Draw(const Matrix4x4& viewProjectionMatrix) {
	if (!isActive_) {
		return;
	}

	for (const std::unique_ptr<Component>& component : components_) {
		if (component->IsEnabled()) {
			component->Draw(viewProjectionMatrix);
		}
	}
}

// ════════════════════════════════════════════════════════════════════
// Transform からワールド行列を作る
//   座標変換の基本どおり Scale → Rotate → Translate の順
// ════════════════════════════════════════════════════════════════════
Matrix4x4 GameObject::GetWorldMatrix() const {
	return MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
//   Transform は手書き、それ以外は各コンポーネントに任せる
//   ★ここが「コンポーネント式にした見返り」そのもの
// ════════════════════════════════════════════════════════════════════
void GameObject::DrawInspector() {
#ifdef USE_IMGUI

	// ───── 名前とアクティブ ─────
	ImGui::Checkbox("##active", &isActive_);
	ImGui::SameLine();
	ImGui::Text("%s", name_.c_str());
	ImGui::Separator();

	// ───── Transform（コンポーネントではないので手書き） ─────
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("translate", &transform_.translate.x, 0.01f);
		ImGui::DragFloat3("rotate", &transform_.rotate.x, 0.01f);
		ImGui::DragFloat3("scale", &transform_.scale.x, 0.01f);
	}

	// ───── 各コンポーネント ─────
	for (size_t i = 0; i < components_.size(); ++i) {
		Component* component = components_[i].get();

		// 同じラベル（"##enabled" など）が衝突しないよう番号で区別する
		ImGui::PushID(static_cast<int>(i));

		bool enabled = component->IsEnabled();
		if (ImGui::Checkbox("##enabled", &enabled)) {
			component->SetEnabled(enabled);
		}
		ImGui::SameLine();

		if (ImGui::CollapsingHeader(component->GetTypeName())) {
			component->DrawInspector();
		}

		ImGui::PopID();
	}

#endif
}