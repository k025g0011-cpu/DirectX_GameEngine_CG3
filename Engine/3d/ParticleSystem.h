#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

#include "BlendMode.h"
#include "EngineContext.h"
#include "MathUtility.h"       // Transform / Matrix4x4
#include "ModelData.h"
#include "Object3dStructs.h"   // Material / TransformationMatrix

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

	/// <summary>Transform から行列を計算して GPU 側の配列に書き込む</summary>
	void Update(const Matrix4x4& viewProjectionMatrix);

	/// <summary>描画。インスタンス数ぶんを1回のドローコールで描く</summary>
	void Draw();

	// ────────────────────────────────────────────────────────────────
	// 粒の操作
	// ────────────────────────────────────────────────────────────────
	uint32_t GetMaxInstanceCount() const { return maxInstanceCount_; }
	uint32_t GetInstanceCount() const { return instanceCount_; }

	/// <summary>実際に描く粒の数。最大数を超えたら止まる</summary>
	void SetInstanceCount(uint32_t count);

	/// <summary>index 番目の粒の Transform。参照なのでそのまま書き換えられる</summary>
	Transform& GetTransform(uint32_t index);

	/// <summary>全粒で共有するマテリアル（色・UV）</summary>
	Material* GetMaterialData() const { return materialData_; }

	/// <summary>合成方法。炎や光なら Add、煙なら Normal</summary>
	BlendMode GetBlendMode() const { return blendMode_; }
	void SetBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }

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
	TransformationMatrix* instancingData_ = nullptr;   // Map したままの書き込み先
	uint32_t instancingSrvIndex_ = 0;

	// ────────────────────────────────────────────────────────────────
	// CPU 側の粒（これを編集して Update で GPU へ送る）
	// ────────────────────────────────────────────────────────────────
	std::vector<Transform> transforms_;
	uint32_t maxInstanceCount_ = 0;
	uint32_t instanceCount_ = 0;

	// 合成方法。パーティクルの性質なので、描画のたびに外から渡すのではなく自分で持つ
	BlendMode blendMode_ = BlendMode::Add;
};

