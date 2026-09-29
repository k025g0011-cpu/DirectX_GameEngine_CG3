#include "Model.h"

#include <cassert>
#include <cstring>

#include "DirectXCommon.h"
#include "TextureManager.h"

// ════════════════════════════════════════════════════════════════════
// 初期化
//   ModelData（CPU 側のデータ）を受け取って、GPU リソースを作る
//   ※ modelData は作り終わったら不要になるので、参照で受け取って持ち続けない
// ════════════════════════════════════════════════════════════════════
void Model::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
    const ModelData& modelData) {

    dxCommon_ = dxCommon;
    textureManager_ = textureManager;

    // 空のモデルは描画できないので、ここで止める
    assert(!modelData.vertices.empty());
    assert(!modelData.materials.empty());
    assert(!modelData.meshes.empty());

    // ════════════════════════════════════════
    // 頂点バッファ（モデル全体で1本）
    // ════════════════════════════════════════
    size_t vertexSize = sizeof(VertexData) * modelData.vertices.size();

    vertexResource_ = dxCommon_->CreateBufferResource(vertexSize);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(vertexSize);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, modelData.vertices.data(), vertexSize);

    // ════════════════════════════════════════
    // インデックスバッファ（indices が空なら作らない）
    //   obj は今インデックスを作っていないので、その場合 indexResource_ は nullptr のまま
    // ════════════════════════════════════════
    if (!modelData.indices.empty()) {
        size_t indexSize = sizeof(uint32_t) * modelData.indices.size();

        indexResource_ = dxCommon_->CreateBufferResource(indexSize);
        indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = UINT(indexSize);
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;   // uint32_t なので Stride は不要

        uint32_t* indexData = nullptr;
        indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        std::memcpy(indexData, modelData.indices.data(), indexSize);
    }

    // ════════════════════════════════════════
    // 座標変換行列の定数バッファ（VS b0）
    // ════════════════════════════════════════
    transformationMatrixResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
    transformationMatrixResource_->Map(0, nullptr,
        reinterpret_cast<void**>(&transformationMatrixData_));
    transformationMatrixData_->WVP = MakeIdentity4x4();
    transformationMatrixData_->World = MakeIdentity4x4();

    // ════════════════════════════════════════
    // マテリアルごとに「定数バッファ（PS b0）」と「テクスチャ」を用意する
    //   マテリアルが1個でも100個でも、同じループで処理できる
    // ════════════════════════════════════════
    materials_.resize(modelData.materials.size());   // 先に必要な数だけ確保しておく

    for (size_t i = 0; i < modelData.materials.size(); ++i) {
        const MaterialData& src = modelData.materials[i];   // 読み取り元（CPU データ）
        MaterialResource& dst = materials_[i];              // 書き込み先（★ & 必須）

        // 定数バッファ
        dst.resource = dxCommon_->CreateBufferResource(sizeof(Material));
        dst.resource->Map(0, nullptr, reinterpret_cast<void**>(&dst.data));
        dst.data->color = src.color;             // mtl の Kd + d
        dst.data->enableLighting = true;         // 3D モデルは基本ライティングする
        dst.data->uvTransform = MakeIdentity4x4();

        // テクスチャ
        //   mtl に map_Kd が無いモデルは白1x1で代用する（白は乗算の単位元なので Kd がそのまま出る）
        std::string texturePath = src.textureFilePath;
        if (texturePath.empty()) {
            texturePath = "resources/white1x1.png";
        }
        dst.textureSrvIndex = textureManager_->Load(texturePath);
    }

    // ════════════════════════════════════════
    // 描画単位はそのままコピーして持つ（GPU リソースではないので変換不要）
    // ════════════════════════════════════════
    meshes_ = modelData.meshes;
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   RootSignature / PSO / トポロジは呼び出し側が設定済みであること
//   平行光源（b1）もシーン共通なので呼び出し側が設定済みであること
// ════════════════════════════════════════════════════════════════════
void Model::Draw(const Matrix4x4& worldMatrix, const Matrix4x4& viewProjectionMatrix) {

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

    // ────────────────────────────────────────
    // ① モデル全体で1回だけ設定するもの
    // ────────────────────────────────────────
    transformationMatrixData_->WVP = worldMatrix * viewProjectionMatrix;
    transformationMatrixData_->World = worldMatrix;

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    if (indexResource_) {
        commandList->IASetIndexBuffer(&indexBufferView_);
    }
    commandList->SetGraphicsRootConstantBufferView(
        kRootParameterTransform, transformationMatrixResource_->GetGPUVirtualAddress());

    // ────────────────────────────────────────
    // ② Mesh ごとに、マテリアルを切り替えて描く
    // ────────────────────────────────────────
    for (const Mesh& mesh : meshes_) {

        const MaterialResource& material = materials_[mesh.materialIndex];

        commandList->SetGraphicsRootConstantBufferView(
            kRootParameterMaterial, material.resource->GetGPUVirtualAddress());// シェーダーにルートパラメータ[0]を介して、角メッシュのmaterialResourceアドレスを渡す
        commandList->SetGraphicsRootDescriptorTable(
            kRootParameterTexture, textureManager_->GetSrvGpuHandle(material.textureSrvIndex)); // シェーダーにルートパラメータ[2]を介して、srvDescripterHeap内の描画予定のTextureアドレスを渡す

        // 描画！(DrawCall/ドローコール)
        // インデックスを持っているモデル(球)と、持っていないモデル(obj)で呼ぶ関数が変わる
        /*
        第1引数：読むインデックスの数。6個 ÷ 3 = 三角形2枚 = 四角形1枚
        第2引数：インスタンス数。同じ形を何体描くか。今は1体
        第3引数：インデックスバッファの何番目から読むか。先頭からなので0
        第4引数：インデックスの値に足すオフセット。頂点バッファの先頭から使うので0
        第5引数：SV_InstanceID の開始番号。インスタンシングしないので0

        */


        if (mesh.indexCount > 0) {
            commandList->DrawIndexedInstanced(mesh.indexCount, 1, mesh.startIndex, 0, 0);
        } else {
            commandList->DrawInstanced(mesh.vertexCount, 1, mesh.startVertex, 0);
        }
    }
}