#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "MathUtility.h"
#include "Object3dStructs.h"   // VertexData / Material / TransformationMatrix

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class DirectXCommon;
class TextureManager;

// ════════════════════════════════════════════════════════════════════
// 2D スプライト（画面に貼る1枚の四角形）
//
//   座標系は画面ピクセル。左上が (0, 0)、右下が (1280, 720)
//   大きさは頂点に、位置と回転はワールド行列に持たせる
//
//   ※ RootSignature / PSO は呼び出し側が設定済みであること
//      （Object3dPipeline::SetPipeline() を先に呼ぶ）
// ════════════════════════════════════════════════════════════════════
class Sprite {
public:
	/// <summary>初期化</summary>
	/// <param name="dxCommon">DirectX基盤（所有しない）</param>
	/// <param name="textureManager">テクスチャ管理（所有しない）</param>
	/// <param name="textureSrvIndex">TextureManager::Load が返した番号</param>
	/// <param name="size">表示するピクセルサイズ</param>
	void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
		uint32_t textureSrvIndex, const Vector2& size);

	/// <summary>描画する。頂点と行列の更新も中で行う</summary>
	void Draw();

	// ────────────────────────────────────────────────────────────────
	// 位置・回転・大きさ
	// ────────────────────────────────────────────────────────────────
	const Vector2& GetPosition() const { return position_; }
	void SetPosition(const Vector2& position) { position_ = position; }

	/// <summary>回転角（ラジアン）。画面に垂直な軸まわり</summary>
	float GetRotation() const { return rotation_; }
	void SetRotation(float rotation) { rotation_ = rotation; }

	const Vector2& GetSize() const { return size_; }
	void SetSize(const Vector2& size) { size_ = size; }

	/// <summary>アンカーポイント。(0,0)=左上 (0.5,0.5)=中心 (1,1)=右下</summary>
	const Vector2& GetAnchorPoint() const { return anchorPoint_; }
	void SetAnchorPoint(const Vector2& anchorPoint) { anchorPoint_ = anchorPoint; }

	// ────────────────────────────────────────────────────────────────
	// 見た目
	// ────────────────────────────────────────────────────────────────
	void SetTextureIndex(uint32_t textureSrvIndex) { textureSrvIndex_ = textureSrvIndex; }
	uint32_t GetTextureIndex() const { return textureSrvIndex_; }

	/// <summary>マテリアルを直接編集する（色・UV変換。ImGui 用）</summary>
	Material* GetMaterialData() { return materialData_; }

private:
	/// <summary>size_ と anchorPoint_ から4頂点を作り直す</summary>
	void UpdateVertices();

	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;
	TextureManager* textureManager_ = nullptr;

	// 頂点バッファ（4頂点。Mapしっぱなしで毎フレーム上書きする）
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	VertexData* vertexData_ = nullptr;

	// インデックスバッファ（6個。四角形＝三角形2枚。作ったら変わらない）
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	// 座標変換の定数バッファ（VS b0）
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	// マテリアルの定数バッファ（PS b0）
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;

	// 使用するテクスチャ（SRV ヒープ上の番号）
	uint32_t textureSrvIndex_ = 0;

	// ────────────────────────────────────────────────────────────────
	// 見た目を決める値
	// ────────────────────────────────────────────────────────────────
	Vector2 position_{ 0.0f, 0.0f };        // 画面上の位置（ピクセル）
	float   rotation_ = 0.0f;               // 回転（ラジアン）
	Vector2 size_{ 100.0f, 100.0f };        // 大きさ（ピクセル）
	Vector2 anchorPoint_{ 0.0f, 0.0f };     // 基準点。(0,0)=左上

	// 平行投影行列。画面サイズが変わらないので初期化時に1回作る
	Matrix4x4 projectionMatrix_{};
};