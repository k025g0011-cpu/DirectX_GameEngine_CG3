#pragma once
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>
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
    void Draw(D3D12_GPU_VIRTUAL_ADDRESS transformationMatrixAddress);

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

    std::vector<MaterialResource> materials_;
    std::vector<Mesh>             meshes_;
};