#pragma once

#include <cstdint>
#include <d3d12.h>

// ════════════════════════════════════════════════════════════════════
// ブレンドモード
//   PS の出力（Src）と、すでに画面にある色（Dest）をどう合成するか
//
//   どのパイプラインでも意味が変わらないので、クラスの外に置く
//   （Object3d / Particle / 将来の UI などが共通で使う）
// ════════════════════════════════════════════════════════════════════
enum class BlendMode : uint32_t {
	None,       // ブレンドしない。Src をそのまま上書き（不透明な物体）
	Normal,     // 通常のαブレンド  Src×α + Dest×(1-α)   （半透明）
	Add,        // 加算              Src×α + Dest          （光・炎・魔法）
	Subtract,   // 減算              Dest - Src×α          （影・煙）
	Multiply,   // 乗算              Src × Dest            （焼き込み）
	Screen,     // スクリーン        Src×(1-Dest) + Dest   （明るく合成）

	kMaxCount,  // ★種類数。配列のサイズに使う。必ず最後に置くこと
};

/// <summary>ブレンドモードに対応した BlendState を作る</summary>
/// <param name="blendMode">合成方法。kMaxCount を渡すのは呼び出し側のバグ</param>
/// <returns>PSO の BlendState にそのまま入れられる設定</returns>
D3D12_BLEND_DESC CreateBlendDesc(BlendMode blendMode);

