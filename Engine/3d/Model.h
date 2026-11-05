#pragma once
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>
#include <span>
#include "MathUtility.h" 
#include "Object3dStructs.h"
#include "ModelData.h" 

class DirectXCommon;
class TextureManager;

// ================================================================
// Model クラス
// ================================================================
class Model {
public:

    // ルートパラメータ番号は Object3dPipeline が決める
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
        const ModelData& modelData);

    /// <summary> 渡された行列CBを使って描画する</summary>
    /// <param name="transformationMatrixAddress">
    ///   ModelInstance が持つ TransformationMatrix の GPU アドレス
    /// </param>
    /// <param name="meshIndices">
    ///   描く Mesh を絞る。空なら全部描く
    ///   ノードごとに分けて描くときに、そのノードが持つ Mesh だけを渡す
    /// </param>
    void Draw(D3D12_GPU_VIRTUAL_ADDRESS transformationMatrixAddress,
        std::span<const uint32_t> meshIndices = {});

    uint32_t  GetMaterialCount() const { return static_cast<uint32_t>(materials_.size()); }
    uint32_t  GetMeshCount() const { return static_cast<uint32_t>(meshes_.size()); }
    Material* GetMaterialData(uint32_t index) { return materials_[index].data; }

    /// <summary>マテリアル名（モデルファイルに書かれていた名前。表示用）</summary>
    const std::string& GetMaterialName(uint32_t index) const { return materials_[index].name; }

    /// <summary>その Mesh が使うマテリアルの添字</summary>
    ///   「このオブジェクトが実際に使うマテリアルはどれか」を調べるのに使う
    uint32_t GetMeshMaterialIndex(uint32_t meshIndex) const { return meshes_[meshIndex].materialIndex; }

    // ────────────────────────────────────────────────────────────────
    // モデルファイルの中の階層
    //   ModelData は Initialize が終わると捨てられるので、ここに残しておく
    //   GPU リソースではない、ただの CPU データ
    // ────────────────────────────────────────────────────────────────
    uint32_t GetNodeCount() const { return static_cast<uint32_t>(nodes_.size()); }
    const ModelNode& GetNode(uint32_t index) const { return nodes_[index]; }

private:
    /// <summary>Mesh 1つを描く（マテリアル切り替え＋ドローコール）</summary>
    void DrawMesh(const Mesh& mesh);

    // マテリアル1個ぶんの GPU リソース
    struct MaterialResource {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Material* data = nullptr;
        uint32_t  textureSrvIndex = 0;

        // モデルファイルに書かれていた名前（GPU には送らない。表示用）
        std::string name;
    };

    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    // 頂点バッファ（モデル全体で1個）
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // インデックスバッファ（使わないモデルでは nullptr のまま）
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    std::vector<MaterialResource> materials_;
    std::vector<Mesh>             meshes_;

    // モデルファイルの階層（[0] がルート。階層を持たないモデルでは空）
    std::vector<ModelNode>        nodes_;
};