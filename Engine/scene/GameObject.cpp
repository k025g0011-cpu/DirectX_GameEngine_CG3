#include "GameObject.h"

#include "EngineContext.h"
#include <algorithm>   // std::remove

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
void GameObject::Draw(const CameraData& camera) {
	if (!isActive_) {
		return;
	}

	for (const std::unique_ptr<Component>& component : components_) {
		if (component->IsEnabled()) {
			component->Draw(camera);
		}
	}
}

// ════════════════════════════════════════════════════════════════════
// Transform からワールド行列を作る
//   座標変換の基本どおり Scale → Rotate → Translate の順
//
// 親の変換まで合成したワールド行列
//   親をさかのぼって掛けていく。親が無ければローカルがそのままワールド
// ════════════════════════════════════════════════════════════════════
Matrix4x4 GameObject::GetWorldMatrix() const {

	const Matrix4x4 local = GetLocalMatrix();

	if (parent_ == nullptr) {
		return local;
	}

	//   行ベクトル規約なので「自分が先、親が後」
	//     v × 自分 × 親 … と左から順に適用される
	return local * parent_->GetWorldMatrix();
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
//   Transform は手書き、それ以外は各コンポーネントに任せる
//   ここが「コンポーネント式にした見返り」そのもの
// ════════════════════════════════════════════════════════════════════
void GameObject::DrawInspector() {
#ifdef USE_IMGUI

	// ───── 名前とアクティブ ─────
	ImGui::Checkbox("##active", &isActive_);
	ImGui::SameLine();
	ImGui::Text("%s", name_.c_str());

	//   親子になっていると同名・同構成のオブジェクトが並ぶので、
	//   どれを編集しているかを所属で示す
	if (parent_ != nullptr) {
		ImGui::SameLine();
		ImGui::TextDisabled("(親: %s)", parent_->GetName().c_str());
	}

	if (!children_.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("(子 %d 個)", static_cast<int>(children_.size()));
	}

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

// ════════════════════════════════════════════════════════════════════
// デバッグ用の線
//   Draw と同じく、自分では何もせずコンポーネントに配るだけ
// ════════════════════════════════════════════════════════════════════
void GameObject::DrawGizmos(PrimitiveDrawer& drawer) {
	if (!isActive_) {
		return;
	}

	for (const std::unique_ptr<Component>& component : components_) {
		if (component->IsEnabled()) {
			component->DrawGizmos(drawer);
		}
	}
}

// ════════════════════════════════════════════════════════════════════
// このオブジェクト単体の変換（親を含まない）
// ════════════════════════════════════════════════════════════════════
Matrix4x4 GameObject::GetLocalMatrix() const {
	return MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
}


// ════════════════════════════════════════════════════════════════════
// 親を設定する
// ════════════════════════════════════════════════════════════════════
void GameObject::SetParent(GameObject* parent) {

	if (parent == this) {
		return;   // 自分自身は親にできない
	}

	// ────────────────────────────────────────────────────────────────
	// 自分の子孫を親にすると輪ができ、GetWorldMatrix が無限再帰する
	//   候補の親をさかのぼって、自分が出てきたら拒否する
	// ────────────────────────────────────────────────────────────────
	for (GameObject* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent_) {
		if (ancestor == this) {
			return;
		}
	}

	// ────────────────────────────────────────────────────────────────
	// いまの親の子リストから自分を外す
	// ────────────────────────────────────────────────────────────────
	if (parent_ != nullptr) {
		std::vector<GameObject*>& siblings = parent_->children_;
		siblings.erase(
			std::remove(siblings.begin(), siblings.end(), this), siblings.end());
	}

	// ────────────────────────────────────────────────────────────────
	// 新しい親に登録する
	// ────────────────────────────────────────────────────────────────
	parent_ = parent;

	if (parent_ != nullptr) {
		parent_->children_.push_back(this);
	}
}