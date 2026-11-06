#include "BlendMode.h"

#include <cassert>

// ════════════════════════════════════════════════════════════════════
// ブレンドモードに応じた BlendState を作る
//
//   最終的な色 = Src × SrcBlend  〔BlendOp〕  Dest × DestBlend
//     Src  … PixelShader が出力した色
//     Dest … すでにレンダーターゲットにある色
//
//   ＜モード＞     ＜SrcBlend＞      ＜BlendOp＞      ＜DestBlend＞
//    None          —（無効）         —                —
//    Normal        SRC_ALPHA         ADD              INV_SRC_ALPHA
//    Add           SRC_ALPHA         ADD              ONE
//    Subtract      SRC_ALPHA         REV_SUBTRACT     ONE
//    Multiply      ZERO              ADD              SRC_COLOR
//    Screen        INV_DEST_COLOR    ADD              ONE
// ════════════════════════════════════════════════════════════════════
D3D12_BLEND_DESC CreateBlendDesc(BlendMode blendMode) {

	D3D12_BLEND_DESC desc{};

	// すべての色要素（RGBA）を書き込む。全モード共通
	desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// ブレンドなしは BlendEnable を FALSE のままにして即返す
	if (blendMode == BlendMode::None) {
		return desc;
	}

	desc.RenderTarget[0].BlendEnable = TRUE;

	// アルファ値の合成方法は全モード共通
	//   RGB とアルファは別々に設定できる。アルファは通常の合成で固定しておく
	desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

	switch (blendMode) {

		// 通常のαブレンド  Src×α + Dest×(1-α)
	case BlendMode::Normal:
		desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		break;

		// 加算  Src×α + Dest   … 光・炎・魔法エフェクト
	case BlendMode::Add:
		desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		desc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		break;

		// 減算  Dest - Src×α   … 影・煙
		//   REV_SUBTRACT は「Dest - Src」。SUBTRACT だと「Src - Dest」になる
	case BlendMode::Subtract:
		desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		desc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		break;

		// 乗算  Src × Dest     … 焼き込み・暗くする
		//   Src 側を 0 倍して消し、Dest 側に Src の色を掛ける
	case BlendMode::Multiply:
		desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
		desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		desc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
		break;

		// スクリーン  Src×(1-Dest) + Dest  … 明るく合成（乗算の逆）
	case BlendMode::Screen:
		desc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		desc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		break;

	default:
		// None は上で return 済み。kMaxCount が来たら呼び出し側のバグ
		assert(false);
		break;
	}

	return desc;
}