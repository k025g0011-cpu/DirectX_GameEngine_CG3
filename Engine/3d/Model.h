#pragma once
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>
#include "MathUtility.h" 
#include "Object3dStructs.h"

class DirectXCommon;
class TextureManager;

// ================================================================
// モデルのデータ構造（CPU 側）
// ================================================================

// ════════════════════════════════════════
// MaterialData構造体
// mtlファイルから読み込んだマテリアル情報
// ※ 今はテクスチャのパスだけ。将来 Kd(色) や法線マップのパスが増える
// ════════════════════════════════════════
struct MaterialData {

    // マテリアル名（mtl の newmtl。obj の usemtl と照合するのに使う）
    std::string name;

    std::string textureFilePath;   // 使用するテクスチャの相対パス（"resources/uvChecker.png" の形）

    // 拡散色（mtl の Kd）＋ 不透明度（mtl の d）
    //   mtl に書かれていない場合は白・不透明のまま
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

// ════════════════════════════════════════════════════════════════════
// Mesh構造体 ＝「1回のドローコールで描く、ひとまとまりの面」
//   マテリアルが変わる／オブジェクトが分かれる、どちらの理由で区切られても Mesh
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
// objファイルから読み取ったモデル全体のデータ（CPU 側）
//   頂点・インデックスは全 Mesh ぶんが1本に並ぶ
// ════════════════════════════════════════════════════════════════════
struct ModelData {
    std::vector<VertexData>   vertices;    // 頂点（全 Mesh ぶん）
    std::vector<uint32_t>     indices;     // インデックス（空なら未使用）
    std::vector<MaterialData> materials;   // マテリアル一覧
    std::vector<Mesh>         meshes;      // 描画単位の一覧
};


// ================================================================
// Model クラス
// ================================================================
class Model {
public:
    // main.cpp の RootSignature の並び順に依存している
    static constexpr uint32_t kRootParameterMaterial = 0;   // PS b0
    static constexpr uint32_t kRootParameterTransform = 1;   // VS b0
    static constexpr uint32_t kRootParameterTexture = 2;   // PS t0

    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
        const ModelData& modelData);
    void Draw(const Matrix4x4& worldMatrix, const Matrix4x4& viewProjectionMatrix);

    uint32_t  GetMaterialCount() const { return static_cast<uint32_t>(materials_.size()); }
    uint32_t  GetMeshCount() const { return static_cast<uint32_t>(meshes_.size()); }
    Material* GetMaterialData(uint32_t index) { return materials_[index].data; }

private:
    // マテリアル1個ぶんの GPU リソース
    struct MaterialResource {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Material* data = nullptr;
        uint32_t  textureSrvIndex = 0;
    };

    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    // 頂点バッファ（モデル全体で1個）
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // インデックスバッファ（使わないモデルでは nullptr のまま）
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    // ワールド行列の定数バッファ（G-3 まではモデル1体ぶんしか持てない）
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    TransformationMatrix* transformationMatrixData_ = nullptr;

    std::vector<MaterialResource> materials_;
    std::vector<Mesh>             meshes_;
};