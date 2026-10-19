#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "BlendMode.h"

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class DirectXCommon;

// ════════════════════════════════════════════════════════════════════
// 3Dモデル・スプライト共通の描画パイプライン
//   RootSignature（引数の形）と PSO（描画設定一式）を持つ
//   ブレンドモードごとに PSO を用意し、切り替えて使う
// ════════════════════════════════════════════════════════════════════
class Object3dPipeline {
public:
	// ────────────────────────────────────────────────────────────────
	// ブレンドモードは BlendMode.h に移動した
	//   Object3dPipeline::BlendMode::Add という既存の書き方を
	//   壊さないために、別名だけ残しておく
	//   （全部の呼び出しが BlendMode:: になったら、この1行も消す）
	// ────────────────────────────────────────────────────────────────
	using BlendMode = ::BlendMode;

	// ────────────────────────────────────────────────────────────────
	// ルートパラメータ番号
	//   RootSignature を作っているのがこのクラスなので、番号もここが公開する
	// ────────────────────────────────────────────────────────────────
	static constexpr uint32_t kRootParameterMaterial = 0;   // PS b0  Material
	static constexpr uint32_t kRootParameterTransform = 1;   // VS b0  TransformationMatrix
	static constexpr uint32_t kRootParameterTexture = 2;   // PS t0  Texture（テーブル）
	static constexpr uint32_t kRootParameterLight = 3;   // PS b1  DirectionalLight

	/// <summary>RootSignature とすべての PSO を作る</summary>
	void Initialize(DirectXCommon* dxCommon);

	/// <summary>コマンドリストにパイプラインを設定する</summary>
	/// <param name="blendMode">合成方法。省略時はブレンドなし</param>
	void SetPipeline(BlendMode blendMode = BlendMode::None);

	/// <summary>RootSignature を取得する（デバッグ表示や派生パイプライン用）</summary>
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