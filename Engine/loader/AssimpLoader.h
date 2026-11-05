#pragma once

#include <string>
#include "ModelData.h"

// ════════════════════════════════════════════════════════════════════
// assimp を使ったモデル読み込み
//   .obj / .gltf / .glb に対応（配布 assimp がこの2形式のみ対応）
//   ModelData を返すだけなので、Model / ModelManager は assimp を知らない
// ════════════════════════════════════════════════════════════════════
class AssimpLoader{
public:
	/// <summary>モデルファイルを読み込んで ModelData を作る</summary>
	/// <param name="filePath">resources/suzanne.obj のような相対パス</param>
	static ModelData Load(const std::string& filePath);

	// 道具箱なのでインスタンスは作らせない
	AssimpLoader() = delete;
};

