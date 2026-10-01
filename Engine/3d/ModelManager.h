#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Model.h"

// 前方宣言
class DirectXCommon;
class TextureManager;

// ════════════════════════════════════════════════════════════════════
// モデルの読み込みと管理
//   パスを渡すと Model* が返る
//   同じパスを2回渡しても、読み込みは1回だけ
//
//   ※ 返した Model* は ModelManager が生きている間だけ有効
// ════════════════════════════════════════════════════════════════════
class ModelManager {
public:
	void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager);

	/// <summary>obj ファイルを読み込む。既に読み込み済みなら再利用する</summary>
	/// <param name="filePath">"resources/suzanne.obj" のような、フォルダを含むパス</param>
	/// <returns>Model へのポインタ。所有権は渡さない</returns>
	Model* Load(const std::string& filePath);

	/// <summary>プログラムで作った ModelData を登録する（球などの基本形状）</summary>
	/// <param name="name">識別用の名前。重複したら既存のものを返す</param>
	Model* Create(const std::string& name, const ModelData& modelData);

	// ────────────────────────────────────────────────────────────────
	// 一覧（エディタのアセット一覧・ImGui 表示用）
	// ────────────────────────────────────────────────────────────────
	uint32_t GetModelCount() const { return static_cast<uint32_t>(models_.size()); }
	const std::string& GetModelName(uint32_t index) const { return models_[index].name; }
	Model* GetModel(uint32_t index) const { return models_[index].model.get(); }

private:
	// 1つのモデルが持つ情報
	struct ModelEntry {
		// 読み込み元のパス、または Create で付けた名前（重複チェック用）
		std::string name;
		// モデル本体。★ unique_ptr なのは「実体を動かさない」ため（下の解説）
		std::unique_ptr<Model> model;
	};

	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;
	TextureManager* textureManager_ = nullptr;

	// 読み込み済みのモデル一覧
	std::vector<ModelEntry> models_;
};