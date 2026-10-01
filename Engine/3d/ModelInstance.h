#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "MathUtility.h"
#include "Object3dStructs.h"   // TransformationMatrix

// 前方宣言
class DirectXCommon;
class Model;

// ════════════════════════════════════════════════════════════════════
// モデル1体ぶんの情報
//   形とマテリアルは Model（共有）が持ち、こちらは位置だけを持つ
//   同じ Model を指す ModelInstance をいくつでも作れる
// ════════════════════════════════════════════════════════════════════
class ModelInstance {
public:
	/// <summary>初期化</summary>
	/// <param name="dxCommon">DirectX基盤（所有しない）</param>
	/// <param name="model">描画するモデル（所有しない。外で生かしておくこと）</param>
	void Initialize(DirectXCommon* dxCommon, Model* model);

	/// <summary>描画する。行列の更新も中で行う</summary>
	void Draw(const Matrix4x4& viewProjectionMatrix);

	// ────────────────────────────────────────────────────────────────
	// 位置・回転・拡縮
	//   参照を返すので、そのまま編集できる
	//     instance.GetTransform().translate.x += 1.0f;
	//     ImGui::DragFloat3("pos", &instance.GetTransform().translate.x, 0.01f);
	// ────────────────────────────────────────────────────────────────
	Transform& GetTransform() { return transform_; }
	const Transform& GetTransform() const { return transform_; }

	// ────────────────────────────────────────────────────────────────
	// 描画するモデルの差し替え
	// ────────────────────────────────────────────────────────────────
	Model* GetModel() const { return model_; }
	void SetModel(Model* model) { model_ = model; }

	// ────────────────────────────────────────────────────────────────
	// 表示・非表示
	// ────────────────────────────────────────────────────────────────
	bool IsVisible() const { return isVisible_; }
	void SetVisible(bool isVisible) { isVisible_ = isVisible; }

	/// <summary>最後の Draw で使ったワールド行列（当たり判定や子の追従に使う）</summary>
	const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }

private:
	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;
	Model* model_ = nullptr;

	// この1体の位置・回転・拡縮
	Transform transform_{
		{ 1.0f, 1.0f, 1.0f },   // scale
		{ 0.0f, 0.0f, 0.0f },   // rotate
		{ 0.0f, 0.0f, 0.0f },   // translate
	};

	bool isVisible_ = true;

	// この1体ぶんの座標変換の定数バッファ（VS b0）
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	// Draw で作られるワールド行列
	Matrix4x4 worldMatrix_{};
};