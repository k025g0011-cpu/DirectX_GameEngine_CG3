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