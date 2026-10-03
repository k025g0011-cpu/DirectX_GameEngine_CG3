#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "Component.h"
#include "MathUtility.h"
#include "Object3dStructs.h"   // TransformationMatrix
#include "AssetDatabase.h"   // AssetPayload::kModel  
#include "ModelManager.h"    // モデルの読み込み       

// 前方宣言（ポインタとしてしか使わない）
class Model;

// ════════════════════════════════════════════════════════════════════
// モデルを描画するコンポーネント
//   ・Model（頂点とマテリアル）は ModelManager が所有。ここは借りるだけ
//   ・個体ごとの行列用に、自分専用の定数バッファを1つ持つ
//   ・位置や回転は GameObject の Transform から毎フレーム取る
// ════════════════════════════════════════════════════════════════════
class MeshRenderer : public Component {
public:
	/// <summary>描画するモデルを指定して作る</summary>
	/// <param name="model">ModelManager から借りた Model。後から SetModel でも可</param>
	explicit MeshRenderer(Model* model = nullptr) : model_(model) {}

	// ───── Component の実装 ─────
	void Initialize() override;
	void Draw(const Matrix4x4& viewProjectionMatrix) override;
	void DrawInspector() override;
	const char* GetTypeName() const override { return "MeshRenderer"; }

	// ───── 自分固有の機能 ─────
	Model* GetModel() const { return model_; }
	void   SetModel(Model* model) { model_ = model; }

	/// <summary>パスからモデルを読み込んで差し替える（ドロップ用）</summary> 
	void SetModelFromPath(const std::string& modelPath);
private:
	// ModelManager が所有している。delete しない
	Model* model_ = nullptr;

	// 今どのファイルを指しているか（インスペクタ表示とシーン保存用）  
	std::string modelPath_;

	// 個体ごとの行列（WVP と World の128バイト）
	//   同じ Model を複数の GameObject が使っても、ここが別なので干渉しない
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
	TransformationMatrix* transformationMatrixData_ = nullptr;
};