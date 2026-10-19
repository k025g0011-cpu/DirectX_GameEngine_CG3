#pragma once

#include <cstdint>
#include <string>

#include "ModelData.h"

// ════════════════════════════════════════════════════════════════════
// 基本形状の ModelData をプログラムで生成する
//   ファイルを読まずに ModelData を作る点以外、ObjLoader と同じ立場
//   状態を持たないので名前空間にまとめる
// ════════════════════════════════════════════════════════════════════
namespace PrimitiveModel {

	/// <summary>緯度経度で分割した球を作る</summary>
	/// <param name="subdivision">分割数。頂点は (n+1)^2 個、インデックスは n*n*6 個</param>
	/// <param name="texturePath">貼るテクスチャのパス。空ならテクスチャ無し（白扱い）</param>
	/// <returns>インデックス付きの ModelData</returns>
	ModelData CreateSphere(uint32_t subdivision, const std::string& texturePath = "");

	/// <summary>XY 平面に置いた四角形を1枚作る（パーティクルの粒・看板などに使う）</summary>
	/// <param name="size">一辺の長さ。1.0f なら -0.5 〜 +0.5 の範囲に収まる</param>
	/// <param name="texturePath">貼るテクスチャのパス。空ならテクスチャ無し（白扱い）</param>
	/// <returns>インデックスを使わない ModelData（頂点6個）</returns>
	/// <remarks>
	///   四角形1枚ではインデックスの節約効果がほとんど無いため、
	///   頂点6個をそのまま並べる。バッファが1本で済み、使う側が単純になる
	/// </remarks>
	ModelData CreatePlane(float size = 1.0f, const std::string& texturePath = "");

}   // namespace PrimitiveModel