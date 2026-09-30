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

}   // namespace PrimitiveModel