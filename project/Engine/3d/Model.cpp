#include "Model.h"

#include <cassert>
#include <cstring>

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "Object3dPipeline.h" 

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
    // マテリアルごとに「定数バッファ（PS b0）」と「テクスチャ」を用意する
    //   マテリアルが1個でも100個でも、同じループで処理できる
    // ════════════════════════════════════════
    materials_.resize(modelData.materials.size());   // 先に必要な数だけ確保しておく

    for (size_t i = 0; i < modelData.materials.size(); ++i) {
        const MaterialData& src = modelData.materials[i];   // 読み取り元（CPU データ）
        MaterialResource& dst = materials_[i];              // 書き込み先（& 必須）

        // 表示用の名前（ModelData は捨てられるので、ここで写しておく）
        dst.name = src.name;

        // 定数バッファ
        dst.resource = dxCommon_->CreateBufferResource(sizeof(Material));
        dst.resource->Map(0, nullptr, reinterpret_cast<void**>(&dst.data));
        dst.data->color = src.color;                   // Kd + d
        dst.data->enableLighting = true;               // 3D モデルは基本ライティングする
        dst.data->uvTransform = MakeIdentity4x4();
        dst.data->shininess = src.shininess;           // Ns。ファイルから
        dst.data->specularColor = src.specularColor;   // Ks。ファイルから
        dst.data->specularModel = SpecularModel::Phong;

        // テクスチャ
        //   ① モデルに埋め込まれている（.glb）
        //   ② 外部ファイル（.obj / .gltf）
        //   ③ どちらも無ければ白1x1
        //      （白は乗算の単位元なので、マテリアルの色がそのまま出る）
        if (src.embeddedTextureIndex >= 0) {
            const EmbeddedTexture& embedded =
                modelData.embeddedTextures[src.embeddedTextureIndex];
            dst.textureSrvIndex = textureManager_->LoadFromMemory(
                embedded.name, embedded.bytes.data(), embedded.bytes.size());
        } else {
            std::string texturePath = src.textureFilePath;
            if (texturePath.empty()) {
                texturePath = "resources/white1x1.png";
            }
            dst.textureSrvIndex = textureManager_->Load(texturePath);
        }
    }

    // ════════════════════════════════════════
    // 描画単位と階層はそのままコピーして持つ
    //   どちらも GPU リソースではないので変換不要
    //   modelData はこの関数を抜けると捨てられるので、ここで写しておく必要がある
    // ════════════════════════════════════════
    meshes_ = modelData.meshes;
    nodes_ = modelData.nodes;
}


// ════════════════════════════════════════════════════════════════════
// 描画
//   行列CBは呼び出し側（ModelInstance）が用意する
//   RootSignature / PSO / トポロジも呼び出し側が設定済みであること
//   平行光源（b1）もシーン共通なので呼び出し側が設定済みであること
// ════════════════════════════════════════════════════════════════════
void Model::Draw(D3D12_GPU_VIRTUAL_ADDRESS transformationMatrixAddress,
    std::span<const uint32_t> meshIndices) {

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

    // ────────────────────────────────────────
    // ① モデル全体で1回だけ設定するもの
    // ────────────────────────────────────────
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    if (indexResource_) {
        commandList->IASetIndexBuffer(&indexBufferView_);
    }
    commandList->SetGraphicsRootConstantBufferView(
        Object3dPipeline::kRootParameterTransform, transformationMatrixAddress);

    // ────────────────────────────────────────
    // ② Mesh ごとに、マテリアルを切り替えて描く
    //    meshIndices が空なら全部、指定があればその Mesh だけ
    // ────────────────────────────────────────
    if (meshIndices.empty()) {
        for (const Mesh& mesh : meshes_) {
            DrawMesh(mesh);
        }
    } else {
        for (const uint32_t meshIndex : meshIndices) {
            DrawMesh(meshes_[meshIndex]);
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// Mesh 1つを描く
//   頂点バッファ・インデックスバッファ・行列CB は Draw が設定済み
// ════════════════════════════════════════════════════════════════════
void Model::DrawMesh(const Mesh& mesh) {

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

    const MaterialResource& material = materials_[mesh.materialIndex]; //　各メッシュに対応したマテリアルの情報を取得する

    commandList->SetGraphicsRootConstantBufferView(
        Object3dPipeline::kRootParameterMaterial,
        material.resource->GetGPUVirtualAddress());  // シェーダーにルートパラメータ[0]を介して、各メッシュのmaterialResourceアドレスを渡す
    commandList->SetGraphicsRootDescriptorTable(
        Object3dPipeline::kRootParameterTexture,
        textureManager_->GetSrvGpuHandle(material.textureSrvIndex)); // シェーダーにルートパラメータ[2]を介して、srvDescriptorHeap内の描画予定のTextureアドレスを渡す

    // 描画！(DrawCall/ドローコール)
    // インデックスを持っているモデル(球)と、持っていないモデル(obj)で呼ぶ関数が変わる
    /*
    第1引数：読むインデックスの数。6個 ÷ 3 = 三角形2枚 = 四角形1枚
    第2引数：インスタンス数。同じ形を何体描くか。今は1体
    第3引数：インデックスバッファの何番目から読むか
    第4引数：インデックスの値に足すオフセット（BaseVertexLocation）
            インデックスは Mesh ごとに 0 から始まるので、
               その Mesh の先頭頂点の位置を GPU に足してもらう
    第5引数：SV_InstanceID の開始番号。インスタンシングしないので0

    */
    if (mesh.indexCount > 0) {
        commandList->DrawIndexedInstanced(
            mesh.indexCount, 1, mesh.startIndex, mesh.startVertex, 0);
    } else {
        commandList->DrawInstanced(mesh.vertexCount, 1, mesh.startVertex, 0);
    }
}