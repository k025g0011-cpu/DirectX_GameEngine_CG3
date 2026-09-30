#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

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
	// ブレンドモード
	//   PS の出力（Src）と、すでに画面にある色（Dest）をどう合成するか
	// ────────────────────────────────────────────────────────────────
	enum class BlendMode : uint32_t {
		None,       // ブレンドしない。Src をそのまま上書き（不透明な物体）
		Normal,     // 通常のαブレンド  Src×α + Dest×(1-α)   （半透明）
		Add,        // 加算              Src×α + Dest          （光・炎・魔法）
		Subtract,   // 減算              Dest - Src×α          （影・煙）
		Multiply,   // 乗算              Src × Dest            （焼き込み）
		Screen,     // スクリーン        Src×(1-Dest) + Dest   （明るく合成）

		kMaxCount,  // ★種類数。配列のサイズに使う。必ず最後に置くこと
	};

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