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
	/// <param name="dxCommon">DirectX の基盤</param>
	/// <param name="textureManager">テクスチャの管理者</param>
	/// <param name="textureSrvIndex">貼るテクスチャの SRV 番号</param>
	/// <param name="size">スプライトの大きさ（ピクセル）</param>
	/// <param name="screenSize">画面の大きさ（ピクセル）。平行投影行列に使う</param>
	void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
		uint32_t textureSrvIndex, const Vector2& size, const Vector2& screenSize);

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
	/// <summary>貼るテクスチャを差し替える。切り出し範囲は画像全体に戻る</summary>
	void SetTextureIndex(uint32_t textureSrvIndex);
	uint32_t GetTextureIndex() const { return textureSrvIndex_; }

	// ────────────────────────────────────────────────────────────────
	// テクスチャの切り出し（ピクセル指定）
	//   1枚の画像から一部分だけを貼る。アニメやアトラスに使う
	// ────────────────────────────────────────────────────────────────

	/// <summary>切り出す矩形を指定する（単位はピクセル）</summary>
	/// <param name="leftTop">元画像の左上からの位置</param>
	/// <param name="size">切り出す大きさ</param>
	void SetTextureRect(const Vector2& leftTop, const Vector2& size);

	/// <summary>切り出し範囲を画像全体に戻す</summary>
	void ResetTextureRect();

	/// <summary>スプライトの表示サイズを、切り出しサイズに合わせる</summary>
	void AdjustTextureSize() { size_ = textureRectSize_; }

	const Vector2& GetTextureRectLeftTop() const { return textureRectLeftTop_; }
	const Vector2& GetTextureRectSize() const { return textureRectSize_; }

	/// <summary>元画像の解像度（ピクセル）</summary>
	const Vector2& GetTextureResolution() const { return textureResolution_; }

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

	// ────────────────────────────────────────────────────────────────
	// 切り出し範囲（すべてピクセル単位。UV への変換は UpdateVertices が行う）
	//   size_ は「画面に描く大きさ」、こちらは「画像のどこを取るか」で別物
	// ────────────────────────────────────────────────────────────────
	Vector2 textureRectLeftTop_{ 0.0f, 0.0f };   // 元画像の左上からの位置
	Vector2 textureRectSize_{ 1.0f, 1.0f };      // 切り出す大きさ
	Vector2 textureResolution_{ 1.0f, 1.0f };    // 元画像の解像度。SetTextureIndex で取る

	// 平行投影行列。画面サイズが変わらないので初期化時に1回作る
	Matrix4x4 projectionMatrix_{};
};