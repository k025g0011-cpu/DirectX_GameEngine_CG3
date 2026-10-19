#include "ParticleSystem.h"

#include <cassert>
#include <cstring>

#include "DirectXCommon.h"
#include "Logger.h"
#include "ParticlePipeline.h"
#include "PrimitiveModel.h"
#include "TextureManager.h"

using Microsoft::WRL::ComPtr;

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::Initialize(const EngineContext& context,
	const std::string& texturePath, uint32_t maxInstanceCount) {

	assert(maxInstanceCount > 0);

	context_ = context;
	maxInstanceCount_ = maxInstanceCount;

	// 板ポリ1枚ぶんの頂点データを作る（CPU 側）
	const ModelData modelData = PrimitiveModel::CreatePlane(1.0f, texturePath);

	CreateVertexBuffer(modelData);
	CreateMaterialResource();
	CreateInstancingResource();

	// テクスチャ（空なら白1x1で代用するのは Model と同じ扱い）
	std::string resolvedTexturePath = modelData.materials[0].textureFilePath;
	if (resolvedTexturePath.empty()) {
		resolvedTexturePath = "resources/white1x1.png";
	}
	textureSrvIndex_ = context_.textureManager->Load(resolvedTexturePath);

	// CPU 側の粒を最大数ぶん用意する（中身は原点・等倍・無回転）
	transforms_.resize(maxInstanceCount_);
	for (Transform& transform : transforms_) {
		transform.scale = { 1.0f, 1.0f, 1.0f };
		transform.rotate = { 0.0f, 0.0f, 0.0f };
		transform.translate = { 0.0f, 0.0f, 0.0f };
	}

	instanceCount_ = 0;   // 使う側が SetInstanceCount で決める
}

// ════════════════════════════════════════════════════════════════════
// 頂点バッファ
//   Model::Initialize の前半と似ているが、こちらは板1枚に特化している
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::CreateVertexBuffer(const ModelData& modelData) {

	assert(!modelData.vertices.empty());

	const size_t vertexSize = sizeof(VertexData) * modelData.vertices.size();

	vertexResource_ = context_.dxCommon->CreateBufferResource(vertexSize);
	vertexResource_->SetName(L"ParticleVertexBuffer");

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(vertexSize);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	VertexData* vertexData = nullptr;
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, modelData.vertices.data(), vertexSize);

	vertexCount_ = static_cast<uint32_t>(modelData.vertices.size());
}

// ════════════════════════════════════════════════════════════════════
// マテリアル（PS b0）
//   全インスタンスで1個を共有する。色を変えると全部の粒に効く
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::CreateMaterialResource() {

	materialResource_ = context_.dxCommon->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->enableLighting = false;   // Particle.PS は見ないが、意味を揃えておく
	materialData_->uvTransform = MakeIdentity4x4();
}

// ════════════════════════════════════════════════════════════════════
// Instancing 用のリソースと SRV（この回の本体）
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::CreateInstancingResource() {

	// ────────────────────────────────────────────────────────────────
	// ① 行列を maxInstanceCount_ 個ぶん置けるバッファを作る
	//     UPLOAD ヒープなので Map したまま毎フレーム書き換えられる
	// ────────────────────────────────────────────────────────────────
	const size_t bufferSize = sizeof(TransformationMatrix) * maxInstanceCount_;

	instancingResource_ = context_.dxCommon->CreateBufferResource(bufferSize);
	instancingResource_->SetName(L"ParticleInstancingBuffer");

	instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

	// 最初のフレームで不定にならないよう単位行列で埋めておく
	for (uint32_t index = 0; index < maxInstanceCount_; ++index) {
		instancingData_[index].WVP = MakeIdentity4x4();
		instancingData_[index].World = MakeIdentity4x4();
	}

	// ────────────────────────────────────────────────────────────────
	// ② StructuredBuffer 用の SRV を作る
	//     テクスチャ用の SRV とは設定項目が違う
	// ────────────────────────────────────────────────────────────────
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	// 構造体のレイアウトは自由なので「形式は不明」と伝える
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	// テクスチャではなくバッファとして使う
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;                                    // 先頭から
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;                  // 固定
	srvDesc.Buffer.NumElements = maxInstanceCount_;                     // 何要素ぶんか
	srvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);  // 1要素のバイト数

	// 空き番号は配布係からもらう（固定値を書かない）
	instancingSrvIndex_ = context_.dxCommon->AllocateSrv();

	context_.dxCommon->GetDevice()->CreateShaderResourceView(
		instancingResource_.Get(), &srvDesc,
		context_.dxCommon->GetSrvCpuDescriptorHandle(instancingSrvIndex_));

	Log(context_.dxCommon->GetLogStream(),
		"Particle instancing buffer : srv=" + std::to_string(instancingSrvIndex_)
		+ "  max=" + std::to_string(maxInstanceCount_) + "\n");
}

// ════════════════════════════════════════════════════════════════════
// 更新
//   Transform から行列を作って、GPU 側の配列に書き込む
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::Update(const Matrix4x4& viewProjectionMatrix) {

	// 描く分だけ計算する。描かない粒の行列は計算しても無駄
	for (uint32_t index = 0; index < instanceCount_; ++index) {

		const Transform& transform = transforms_[index];

		const Matrix4x4 worldMatrix =
			MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);

		instancingData_[index].WVP = worldMatrix * viewProjectionMatrix;
		instancingData_[index].World = worldMatrix;
	}
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   粒が何個あっても、ドローコールは1回だけ
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::Draw() {

	// 1個も無いなら何もしない
	if (instanceCount_ == 0) {
		return;
	}

	ID3D12GraphicsCommandList* commandList = context_.dxCommon->GetCommandList().Get();

	// ① パイプライン（RootSignature + PSO + トポロジ）
	context_.particlePipeline->SetPipeline(blendMode_);

	// ② 頂点バッファ（板1枚ぶん）
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// ③ マテリアル（PS b0）
	commandList->SetGraphicsRootConstantBufferView(
		ParticlePipeline::kRootParameterMaterial,
		materialResource_->GetGPUVirtualAddress());

	// ④ 行列の配列（VS t0）… ここが Instancing の肝
	commandList->SetGraphicsRootDescriptorTable(
		ParticlePipeline::kRootParameterInstancing,
		context_.dxCommon->GetSrvGpuDescriptorHandle(instancingSrvIndex_));

	// ⑤ テクスチャ（PS t0）
	commandList->SetGraphicsRootDescriptorTable(
		ParticlePipeline::kRootParameterTexture,
		context_.textureManager->GetSrvGpuHandle(textureSrvIndex_));

	// ⑥ 描画！ 6頂点の板を instanceCount_ 個ぶん、1回の命令で描く
	/*
	第1引数：1インスタンスあたりの頂点数。板ポリなので6
	第2引数：インスタンス数。SV_InstanceID が 0 〜 これ-1 になる
	第3引数：頂点バッファの何番目から読むか
	第4引数：SV_InstanceID の開始番号
	*/
	commandList->DrawInstanced(vertexCount_, instanceCount_, 0, 0);
}

// ════════════════════════════════════════════════════════════════════
// 描く粒の数を決める
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::SetInstanceCount(uint32_t count) {
	// 確保した数を超えると、バッファの外を読んで未定義動作になる
	assert(count <= maxInstanceCount_);
	instanceCount_ = count;
}

// ════════════════════════════════════════════════════════════════════
// index 番目の粒の Transform
// ════════════════════════════════════════════════════════════════════
Transform& ParticleSystem::GetTransform(uint32_t index) {
	assert(index < maxInstanceCount_);
	return transforms_[index];
}