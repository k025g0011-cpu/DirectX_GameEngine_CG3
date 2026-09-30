#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Object3dStructs.h"   // VertexData

// ════════════════════════════════════════════════════════════════════
// モデルのデータ構造（CPU 側）
//   GPU を一切知らない。ファイルを読む側と、GPU へ載せる側の受け渡しに使う
// ════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════
// MaterialData構造体
// mtlファイルから読み込んだマテリアル情報
// ════════════════════════════════════════
struct MaterialData {
    // マテリアル名（mtl の newmtl。obj の usemtl と照合するのに使う）
    std::string name;

    // 使用するテクスチャの相対パス（"resources/uvChecker.png" の形）
    std::string textureFilePath;

    // 拡散色（mtl の Kd）＋ 不透明度（mtl の d）
    //   mtl に書かれていない場合は白・不透明のまま
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

// ════════════════════════════════════════════════════════════════════
// Mesh構造体 ＝「1回のドローコールで描く、ひとまとまりの面」
//   頂点バッファは Model 全体で1本。Mesh は「その中のどの範囲か」を持つ
// ════════════════════════════════════════════════════════════════════
struct Mesh {
    // この Mesh が使うマテリアル（ModelData::materials の添字）
    uint32_t materialIndex = 0;

    // インデックスを使わない描画のとき
    uint32_t startVertex = 0;
    uint32_t vertexCount = 0;

    // インデックスを使う描画のとき（indexCount が 0 ならインデックスを使わない）
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
};

// ════════════════════════════════════════════════════════════════════
// モデル全体のデータ（CPU 側）
//   頂点・インデックスは全 Mesh ぶんが1本に並ぶ
// ════════════════════════════════════════════════════════════════════
struct ModelData {
    std::vector<VertexData>   vertices;    // 頂点（全 Mesh ぶん）
    std::vector<uint32_t>     indices;     // インデックス（空なら未使用）
    std::vector<MaterialData> materials;   // マテリアル一覧
    std::vector<Mesh>         meshes;      // 描画単位の一覧
};