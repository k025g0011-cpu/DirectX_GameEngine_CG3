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