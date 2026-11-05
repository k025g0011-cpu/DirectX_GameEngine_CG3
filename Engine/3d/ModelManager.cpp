#include "ModelManager.h"

#include <cassert>

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "AssimpLoader.h"
#include "Logger.h"

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void ModelManager::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager) {
	assert(dxCommon != nullptr);
	assert(textureManager != nullptr);

	dxCommon_ = dxCommon;
	textureManager_ = textureManager;
}

// ════════════════════════════════════════════════════════════════════
// モデルファイルを読み込む（.obj / .gltf / .glb）
// ════════════════════════════════════════════════════════════════════
Model* ModelManager::Load(const std::string& filePath) {

	// ────────────────────────────────────────
	// ① 既に読み込み済みなら、それを返す
	// ────────────────────────────────────────
	for (const ModelEntry& entry : models_) {
		if (entry.name == filePath) {
			return entry.model.get();
		}
	}

	ModelData modelData = AssimpLoader::Load(filePath);

	// ────────────────────────────────────────
	// ③ Model を作って一覧に登録する
	// ────────────────────────────────────────
	ModelEntry entry{};
	entry.name = filePath;
	entry.model = std::make_unique<Model>();
	entry.model->Initialize(dxCommon_, textureManager_, modelData);

	Model* result = entry.model.get();           // 返す値を先に取っておく
	models_.push_back(std::move(entry));         // unique_ptr はコピーできないので move

	Log(dxCommon_->GetLogStream(),
		"Load model: " + filePath
		+ "  vertices=" + std::to_string(modelData.vertices.size())
		+ "  indices=" + std::to_string(modelData.indices.size())
		+ "  meshes=" + std::to_string(modelData.meshes.size())
		+ "  materials=" + std::to_string(modelData.materials.size())
		+ "\n");

	return result;
}

// ════════════════════════════════════════════════════════════════════
// プログラムで作った ModelData を登録する
// ════════════════════════════════════════════════════════════════════
Model* ModelManager::Create(const std::string& name, const ModelData& modelData) {

	// 既に同じ名前で登録済みなら、それを返す
	for (const ModelEntry& entry : models_) {
		if (entry.name == name) {
			return entry.model.get();
		}
	}

	ModelEntry entry{};
	entry.name = name;
	entry.model = std::make_unique<Model>();
	entry.model->Initialize(dxCommon_, textureManager_, modelData);

	Model* result = entry.model.get();
	models_.push_back(std::move(entry));

	Log(dxCommon_->GetLogStream(), "Create model: " + name + "\n");

	return result;
}