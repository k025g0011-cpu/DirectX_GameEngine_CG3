#pragma once


#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "BlendMode.h"

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class DirectXCommon;

// ════════════════════════════════════════════════════════════════════
// パーティクル専用の描画パイプライン
//   Object3dPipeline と形は似ているが、RootSignature が別物になる
//   （行列を CBV ではなく StructuredBuffer で渡して Instancing する）
//
//   ブレンドの計算式だけは BlendMode.h と共有している
// ════════════════════════════════════════════════════════════════════

class ParticlePipeline{
public:
	// ────────────────────────────────────────────────────────────────
	// ルートパラメータ番号
	//   RootSignature を作っているのがこのクラスなので、番号もここが公開する
	// ────────────────────────────────────────────────────────────────
	static constexpr uint32_t kRootParameterMaterial = 0;   // PS b0  Material
	static constexpr uint32_t kRootParameterInstancing = 1;   // VS t0  行列の配列（テーブル）
	static constexpr uint32_t kRootParameterTexture = 2;   // PS t0  Texture（テーブル）

	/// <summary>RootSignature とすべての PSO を作る</summary>
	void Initialize(DirectXCommon* dxCommon);

	/// <summary>コマンドリストにパイプラインを設定する</summary>
	/// <param name="blendMode">合成方法。省略時はブレンドなし</param>
	void SetPipeline(BlendMode blendMode = BlendMode::None);

	/// <summary>RootSignature を取得する（デバッグ表示用）</summary>
	const Microsoft::WRL::ComPtr<ID3D12RootSignature>& GetRootSignature() const {
		return rootSignature_;
	}

private:
	// 初期化の内訳
	void CreateRootSignature();
	void CreatePipelineStates();

	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;

	// シェーダーへの引数の形。すべての PSO で共通
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

	// ブレンドモードごとの PSO
	Microsoft::WRL::ComPtr<ID3D12PipelineState>
		pipelineStates_[static_cast<uint32_t>(BlendMode::kMaxCount)];

};

