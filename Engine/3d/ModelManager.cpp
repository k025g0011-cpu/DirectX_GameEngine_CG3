#include "ModelManager.h"

#include <cassert>

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "ObjLoader.h"
#include "Logger.h"

// ════════════════════════════════════════════════════════════════════
// このファイルの中だけで使う関数
// ════════════════════════════════════════════════════════════════════
namespace {

	// ════════════════════════════════════════
	// "resources/suzanne.obj" を
	//   フォルダ名 "resources" と ファイル名 "suzanne.obj" に分ける
	//   区切りが無ければフォルダ名は "."（カレント）にする
	// ════════════════════════════════════════
	void SplitPath(const std::string& filePath,
		std::string& outDirectory, std::string& outFilename) {

		// '/' と '\' のどちらでも区切れるようにする
		size_t separatorPos = filePath.find_last_of("/\\");

		if (separatorPos == std::string::npos) {
			outDirectory = ".";
			outFilename = filePath;
		} else {
			outDirectory = filePath.substr(0, separatorPos);
			outFilename = filePath.substr(separatorPos + 1);
		}
	}

}   // namespace

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
// obj ファイルを読み込む
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

	// ────────────────────────────────────────
	// ② パスを分解して obj を読む
	// ────────────────────────────────────────
	std::string directory;
	std::string filename;
	SplitPath(filePath, directory, filename);

	ModelData modelData = ObjLoader::Load(directory, filename);

	// ────────────────────────────────────────
	// ③ Model を作って一覧に登録する
	// ────────────────────────────────────────
	ModelEntry entry{};
	entry.name = filePath;
	entry.model = std::make_unique<Model>();
	entry.model->Initialize(dxCommon_, textureManager_, modelData);

	Model* result = entry.model.get();           // 返す値を先に取っておく
	models_.push_back(std::move(entry));         // unique_ptr はコピーできないので move

	Log(dxCommon_->GetLogStream(), "Load model: " + filePath + "\n");

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