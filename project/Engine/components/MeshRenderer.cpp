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
	transformationMatrixData_->WorldInverseTranspose = MakeIdentity4x4();
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
void MeshRenderer::Draw(const CameraData& camera) {


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
	transformationMatrixData_->WVP = worldMatrix * camera.viewProjectionMatrix;
	transformationMatrixData_->World = worldMatrix;
	//   法線用。非均等スケールでも法線が正しい向きを保つ
	transformationMatrixData_->WorldInverseTranspose = Transpose(Inverse(worldMatrix));
	// ────────────────────────────────────────────────────────────────
	// ③ Model に「この行列で、この Mesh を描いて」と頼む
	//    頂点・マテリアル・テクスチャの設定は Model 側がやる
	//    meshIndices_ が空なら Model 側が全部描く
	// ────────────────────────────────────────────────────────────────
	model_->Draw(transformationMatrixResource_->GetGPUVirtualAddress(), meshIndices_);
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
// ════════════════════════════════════════════════════════════════════
void MeshRenderer::DrawInspector() {
#ifdef USE_IMGUI
	// ════════════════════════════════════════
	// モデル欄（ここにアセットパネルから落とせる）  
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
	ImGui::Text("nodes     : %u", model_->GetNodeCount());

	//   どの Mesh を担当しているか（空＝モデル全体）
	if (meshIndices_.empty()) {
		ImGui::Text("draw      : all");
	} else {
		std::string list;
		for (const uint32_t meshIndex : meshIndices_) {
			if (!list.empty()) {
				list += ", ";
			}
			list += std::to_string(meshIndex);
		}
		ImGui::Text("draw      : %s", list.c_str());
	}

	ImGui::Separator();
	ImGui::TextDisabled("マテリアル（Model 共有）");

	// ────────────────────────────────────────────────────────────────
	// このオブジェクトが実際に描くマテリアルかどうかを判定する
	//   Model は全マテリアルを持つが、このオブジェクトは担当 Mesh ぶんしか描かない
	//   使わないマテリアルを動かしても見た目が変わらないので、見分けが付くようにする
	// ────────────────────────────────────────────────────────────────
	const auto isUsedByThisObject = [this](uint32_t materialIndex) {

		//   Mesh を絞っていない＝モデル全体を描くので、全マテリアルが対象
		if (meshIndices_.empty()) {
			return true;
		}

		for (const uint32_t meshIndex : meshIndices_) {
			if (model_->GetMeshMaterialIndex(meshIndex) == materialIndex) {
				return true;
			}
		}
		return false;
		};

	// マテリアルは Model（共有データ）側にあるので、
	//    ここを変えると「同じモデルを使っている全オブジェクト」に反映される
	for (uint32_t i = 0; i < model_->GetMaterialCount(); ++i) {
		ImGui::PushID(static_cast<int>(i));

		const bool used = isUsedByThisObject(i);

		//   見出しは「[番号] 名前」。名前が無いモデルもあるので代替を用意する
		const std::string& materialName = model_->GetMaterialName(i);
		const std::string header =
			"[" + std::to_string(i) + "] "
			+ (materialName.empty() ? std::string("(名前なし)") : materialName)
			+ (used ? "" : "   - このオブジェクトでは未使用");

		//   使うものだけ開いた状態で出す。使わないものは閉じて灰色にする
		if (!used) {
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		}
		const bool opened = ImGui::CollapsingHeader(header.c_str(),
			used ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
		if (!used) {
			ImGui::PopStyleColor();   // Push したぶんだけ必ず Pop する
		}

		if (opened) {
			Material* material = model_->GetMaterialData(i);

			ImGui::ColorEdit4("color", &material->color.x);
			//   上限は Ns の実値に合わせる。barrel は 413 なので 200 だと切り捨てられていた
			ImGui::DragFloat("光沢度", &material->shininess, 1.0f, 0.0f, 1000.0f, "%.1f");
			//   ImGui は C のライブラリなので int* しか受け取れない
			//     enum class ⇔ int の変換は、この境界だけで行う
			int32_t specularModel = static_cast<int32_t>(material->specularModel);
			if (ImGui::Combo("鏡面反射モデル", &specularModel, "Phong\0Blinn-Phong\0")) {
				material->specularModel = static_cast<SpecularModel>(specularModel);
			}
			//   ハイライトの色。白＝非金属、色付き＝金属 など
			ImGui::ColorEdit3("鏡面反射色", &material->specularColor.x);

			// ライティングの ON/OFF（int32_t なので bool を経由する）
			bool enableLighting = (material->enableLighting != 0);
			if (ImGui::Checkbox("ライティング", &enableLighting)) {
				material->enableLighting = enableLighting ? 1 : 0;
			}
		}

		ImGui::PopID();
	}

#endif
}