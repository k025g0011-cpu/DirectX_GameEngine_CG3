#include "MeshRenderer.h"

#include "GameObject.h"
#include "EngineContext.h"
#include "DirectXCommon.h"
#include "Model.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// ════════════════════════════════════════════════════════════════════
// 初期化
//   AddComponent の中で、owner_ が設定された直後に呼ばれる
// ════════════════════════════════════════════════════════════════════
void MeshRenderer::Initialize() {

	// エンジンの道具は owner 経由で借りる
	DirectXCommon* dxCommon = GetContext().dxCommon;

	// ────────────────────────────────────────────────────────────────
	// 個体ごとの行列用の定数バッファ（128バイト）
	//   UPLOAD ヒープなので Map したまま毎フレーム書き換えてよい
	// ────────────────────────────────────────────────────────────────
	transformationMatrixResource_ =
		dxCommon->CreateBufferResource(sizeof(TransformationMatrix));

	transformationMatrixResource_->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixData_));

	// 最初のフレームで不定にならないよう単位行列を入れておく
	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();
}

// ════════════════════════════════════════════════════════════════════
// パスからモデルを読み込んで差し替える
//   すでに読み込み済みなら ModelManager がキャッシュを返す
// ════════════════════════════════════════════════════════════════════
void MeshRenderer::SetModelFromPath(const std::string& modelPath) {

	Model* model = GetContext().modelManager->Load(modelPath);

	// 読み込みに失敗したら今のモデルを保つ（画面から消えない方が親切）
	if (model == nullptr) {
		return;
	}

	model_ = model;
	modelPath_ = modelPath;
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   isEnabled_ の判定は GameObject::Draw が済ませているので不要
// ════════════════════════════════════════════════════════════════════
void MeshRenderer::Draw(const Matrix4x4& viewProjectionMatrix) {

	// モデルが設定されていなければ何もしない
	if (model_ == nullptr) {
		return;
	}

	// ────────────────────────────────────────────────────────────────
	// ① GameObject の Transform からワールド行列を作ってもらう
	//    自分では Transform を持たない
	// ────────────────────────────────────────────────────────────────
	const Matrix4x4 worldMatrix = GetOwner()->GetWorldMatrix();

	// ────────────────────────────────────────────────────────────────
	// ② 定数バッファに書き込む（Map 済みなのでそのまま代入できる）
	// ────────────────────────────────────────────────────────────────
	transformationMatrixData_->WVP = worldMatrix * viewProjectionMatrix;
	transformationMatrixData_->World = worldMatrix;

	// ────────────────────────────────────────────────────────────────
	// ③ Model に「この行列で描いて」と頼む
	//    頂点・マテリアル・テクスチャの設定は Model 側がやる
	// ────────────────────────────────────────────────────────────────
	model_->Draw(transformationMatrixResource_->GetGPUVirtualAddress());
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
// ════════════════════════════════════════════════════════════════════
void MeshRenderer::DrawInspector() {
#ifdef USE_IMGUI
	// ════════════════════════════════════════
// モデル欄（ここにアセットパネルから落とせる）  ★D-5
// ════════════════════════════════════════
	ImGui::TextDisabled("モデル");

	// 表示文字はパス。コードから直接 Model* を渡された場合はパスが無い
	const char* modelLabel =
		!modelPath_.empty() ? modelPath_.c_str()
		: (model_ != nullptr ? "(コードで設定)" : "(未設定)");

	// ラベルが変わっても ID が変わらないように、ID を固定する
	ImGui::PushID("model");
	ImGui::Button(modelLabel, ImVec2(-1.0f, 0.0f));   // 押しても何も起きない。ドロップ先の枠
	ImGui::PopID();

	// 直前のアイテム（= 上のボタン）をドロップ先にする
	if (ImGui::BeginDragDropTarget()) {

		// 名前が一致したときだけ中身が返る。違う種類なら nullptr
		if (const ImGuiPayload* payload =
			ImGui::AcceptDragDropPayload(AssetPayload::kModel)) {

			// 小包の中身は '\0' 終端の文字列
			SetModelFromPath(static_cast<const char*>(payload->Data));
		}

		ImGui::EndDragDropTarget();   // Begin が true のときだけ呼ぶ
	}

	ImGui::Separator();

	if (model_ == nullptr) {
		ImGui::TextDisabled("(no model)");
		return;
	}

	ImGui::Text("meshes    : %u", model_->GetMeshCount());
	ImGui::Text("materials : %u", model_->GetMaterialCount());

	// マテリアルは Model（共有データ）側にあるので、
	//    ここを変えると「同じモデルを使っている全オブジェクト」に反映される
	for (uint32_t i = 0; i < model_->GetMaterialCount(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		ImGui::ColorEdit4("color", &model_->GetMaterialData(i)->color.x);
		ImGui::PopID();
	}

#endif
}