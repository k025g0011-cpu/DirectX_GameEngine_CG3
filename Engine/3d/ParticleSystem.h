#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <list>
#include <wrl.h>
#include <vector>
#include <memory>

#include "BlendMode.h"
#include "EngineContext.h"
#include "CameraData.h"
#include "Field.h"
#include "MathUtility.h"       // Matrix4x4
#include "ModelData.h"
#include "Object3dStructs.h"   // Material
#include "Particle.h"

// ════════════════════════════════════════════════════════════════════
// パーティクル（板ポリゴンの群れ）
//   ・板1枚ぶんの頂点を共有し、Instancing で一気に描く
//   ・粒ごとに違うのは Transform だけ
//
//   Model を使わない理由：
//     RootSignature が Object3d と違うので、描画コマンドを共有できない
//     また、複数メッシュ・複数マテリアルの仕組みを一切使わない
// ════════════════════════════════════════════════════════════════════
class ParticleSystem{
public:
	/// <summary>初期化</summary>
	/// <param name="context">エンジンの道具一式</param>
	/// <param name="texturePath">粒に貼るテクスチャ</param>
	/// <param name="maxInstanceCount">確保する粒の最大数。これ以上は描けない</param>
	void Initialize(const EngineContext& context,
		const std::string& texturePath, uint32_t maxInstanceCount = 128);

	/// <summary>粒を進める（移動・寿命・削除）。カメラは要らない</summary>
	/// <param name="deltaTime">進める秒数</param>
	void Update(float deltaTime);

	/// <summary>行列を GPU 側へ書き込み、1回のドローコールで描く</summary>
	/// <param name="camera">今のカメラ。ビルボードにビュー行列を使う</param>
	void Draw(const CameraData& camera);

	// ────────────────────────────────────────────────────────────────
	// 粒の操作
	// ────────────────────────────────────────────────────────────────
	/// <summary>GPU 側に確保してある枠の数。これ以上は同時に描けない</summary>
	uint32_t GetMaxInstanceCount() const { return maxInstanceCount_; }

	/// <summary>直前の Update で実際に描いた粒の数</summary>
	uint32_t GetDrawCount() const { return drawCount_; }

	/// <summary>CPU 側に存在する粒の数。上限はない</summary>
	size_t GetCount() const { return particles_.size(); }

	/// <summary>粒を1個追加する。中身は呼ぶ側が組み立てる</summary>
	void Add(const Particle& particle);

	/// <summary>全部消す</summary>
	void Clear();

	// ────────────────────────────────────────────────────────────────
	// 場（Field）
	//   ここに登録された場が、毎フレーム粒に影響を与える
	// ────────────────────────────────────────────────────────────────

	/// <summary>場を追加する。所有権ごと受け取る</summary>
	void AddField(std::unique_ptr<Field> field);

	uint32_t GetFieldCount() const { return static_cast<uint32_t>(fields_.size()); }

	/// <summary>index 番目の場。インスペクタから設定をいじるため</summary>
	Field* GetFieldAt(uint32_t index) const { return fields_[index].get(); }

	/// <summary>全粒で共有するマテリアル（色・UV）</summary>
	Material* GetMaterialData() const { return materialData_; }

	/// <summary>合成方法。炎や光なら Add、煙なら Normal</summary>
	BlendMode GetBlendMode() const { return blendMode_; }
	void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

	/// <summary>板を常にカメラへ向けるか。false にすると板ポリだとバレる</summary>
	bool IsBillboard() const { return useBillboard_; }
	void SetBillboard(bool useBillboard) { useBillboard_ = useBillboard; }
private:
	// 初期化の内訳
	void CreateVertexBuffer(const ModelData& modelData);
	void CreateMaterialResource();
	void CreateInstancingResource();

	// 借りているもの（所有していない）
	EngineContext context_{};

	// ────────────────────────────────────────────────────────────────
	// 板ポリ1枚ぶんの頂点（全インスタンスで共有）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	uint32_t vertexCount_ = 0;

	// ────────────────────────────────────────────────────────────────
	// マテリアルとテクスチャ（全インスタンスで共有）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;
	uint32_t textureSrvIndex_ = 0;

	// ────────────────────────────────────────────────────────────────
	// Instancing 用の行列配列（StructuredBuffer）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
	ParticleForGPU* instancingData_ = nullptr;    // Map したままの書き込み先
	uint32_t instancingSrvIndex_ = 0;

	// ────────────────────────────────────────────────────────────────
	// CPU 側の粒（これを編集して Update で GPU へ送る）
	// ────────────────────────────────────────────────────────────────
	// CPU 側の粒。寿命が尽きたものは Update で本当に erase される
	//   個数に上限はない。GPU に送れるのは maxInstanceCount_ 個まで
	std::list<Particle> particles_;

	uint32_t maxInstanceCount_ = 0;   // GPU 側の枠の数
	uint32_t drawCount_ = 0;          // 今フレーム描いた数（Update が決める）

	// 合成方法。パーティクルの性質なので、描画のたびに外から渡すのではなく自分で持つ
	BlendMode blendMode_ = BlendMode::Add;

	// ビルボードするか。切り替えられるようにする
	bool useBillboard_ = true;

	// この粒たちに影響を与える場。所有する
	std::vector<std::unique_ptr<Field>> fields_;
};

