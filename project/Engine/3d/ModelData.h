#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Object3dStructs.h"   // VertexData

// ════════════════════════════════════════════════════════════════════
// モデルのデータ構造（CPU 側）
//   GPU を一切知らない。ファイルを読む側と、GPU へ載せる側の受け渡しに使う
// ════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════
// 埋め込みテクスチャ
//   .glb のように、画像がモデルファイルの中に入っている場合に使う
//   中身は png / jpg の「ファイルそのもののバイト列」
// ════════════════════════════════════════════════════════════════════
struct EmbeddedTexture {
    // 識別用の名前（TextureManager のキャッシュのキーになる）
    //   "resources/barrel.glb#*0" のように、モデルのパス＋番号で一意にする
    std::string name;

    // 画像ファイルの中身そのまま
    std::vector<uint8_t> bytes;
};

// ════════════════════════════════════════
// MaterialData構造体
// mtlファイルから読み込んだマテリアル情報
// ════════════════════════════════════════
struct MaterialData {
    // マテリアル名（mtl の newmtl / glTF のマテリアル名）
    std::string name;

    // 使用するテクスチャの相対パス（外部ファイルのとき）
    std::string textureFilePath;

    // 埋め込みテクスチャの添字（ModelData::embeddedTextures）
    //   -1 なら埋め込みではない
    int32_t embeddedTextureIndex = -1;

    // 拡散色（mtl の Kd）＋ 不透明度（mtl の d / Tr）
    //   ファイルに書かれていない場合は白・不透明のまま
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    // 鏡面反射色（mtl の Ks）。白なら光源の色がそのまま出る
    Vector3 specularColor = { 1.0f, 1.0f, 1.0f };

    // 光沢度（mtl の Ns）。大きいほど狭く鋭く光る
    float shininess = 20.0f;
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
// ModelNode構造体 ＝ モデルファイルの中の「オブジェクト1個」
//  Blender で置いたオブジェクトが、そのまま1ノードになる
//   ・localMatrix は「親から見た変換」（エンジンの座標系・行ベクトル規約）
//   ・子は 添字で持つ。ポインタだと vector の引っ越しでぶっ壊れる
// ════════════════════════════════════════════════════════════════════
struct ModelNode {
    // 表示名（Blender で付けた名前がそのまま入る）
    std::string name;

    // 親から見た変換
    Matrix4x4 localMatrix = MakeIdentity4x4();

    // このノードが描く Mesh（ModelData::meshes の添字）
    std::vector<uint32_t> meshIndices;

    // 子ノード（ModelData::nodes の添字）
    std::vector<uint32_t> children;
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
    std::vector<EmbeddedTexture> embeddedTextures;   // モデルに同梱された画像
    std::vector<ModelNode>       nodes;              // 階層（[0] がルート。空なら階層なし）
};