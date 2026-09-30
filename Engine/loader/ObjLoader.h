#pragma once

#include <string>

#include "ModelData.h"

// ════════════════════════════════════════════════════════════════════
// obj ファイルの読み込み
//   状態を持たないので、クラスではなく名前空間にまとめる
// ════════════════════════════════════════════════════════════════════
namespace ObjLoader {

	/// <summary>obj ファイルを読み込んで ModelData を作る</summary>
	/// <param name="directoryPath">ファイルが置いてあるフォルダ（例 "resources"）</param>
	/// <param name="filename">ファイル名（例 "suzanne.obj"）</param>
	/// <returns>頂点・マテリアル・描画単位が詰まった ModelData</returns>
	ModelData Load(const std::string& directoryPath, const std::string& filename);

}   // namespace ObjLoader