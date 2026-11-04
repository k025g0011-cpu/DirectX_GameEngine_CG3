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

	// CPU 側の粒は空から始まる。Add で増やしていく
	drawCount_ = 0;   // Update が毎フレーム数え直す
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
	materialData_->shininess = 0.0f; // ライティングしないので使われない
	materialData_->specularColor = Vector3{ 0.0f, 0.0f, 0.0f };   // パーティクルも光らせない
	materialData_->specularModel = SpecularModel::Phong;
}

// ════════════════════════════════════════════════════════════════════
// Instancing 用のリソースと SRV（この回の本体）
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::CreateInstancingResource() {

	// ────────────────────────────────────────────────────────────────
	// ① 行列を maxInstanceCount_ 個ぶん置けるバッファを作る
	//     UPLOAD ヒープなので Map したまま毎フレーム書き換えられる
	// ────────────────────────────────────────────────────────────────
	const size_t bufferSize = sizeof(ParticleForGPU) * maxInstanceCount_;

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
	srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);  // 1要素のバイト数

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
//   粒を進めて、寿命が尽きたものを消すだけ
//   カメラに一切依存しないので、Component::Update() から呼べる
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::Update(float deltaTime) {

	// ループ中に erase するので range-based for は使えない
	for (auto it = particles_.begin(); it != particles_.end(); ) {

		Particle& particle = *it;

		// ─── 場の影響を受ける（速度が変わる） ───
		//   位置を動かす前にやる。今フレームの加速がそのまま移動に反映される
		for (const std::unique_ptr<Field>& field : fields_) {

			if (!field->IsEnabled()) {
				continue;
			}
			if (!field->IsInside(particle.transform.translate)) {
				continue;
			}

			field->Apply(particle, deltaTime);
		}
		// ─── 時間を進める ───
		particle.currentTime += deltaTime;

		// ─── 速度で動かす ───
			//   velocity は「1秒あたりの移動量」なので、経過秒数を掛ける
		particle.transform.translate += particle.velocity * deltaTime;

		// ─── 寿命が尽きたら、本当に消す ───
		if (!particle.IsAlive()) {
			it = particles_.erase(it);   // erase は「次の要素」を返す
			continue;                    // ここで ++it してはいけない
		}

		++it;
	}
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   ① 生きている粒の行列を、GPU 側の配列へ「詰めて」書く
//   ② 描画コマンドを積む。粒が何個あってもドローコールは1回だけ
//
//   GPU 用のバッファに書くのは「描画の準備」なので、ここでやる
//   （MeshRenderer が Draw の中で定数バッファに書くのと同じ扱い）
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::Draw(const CameraData& camera) {

	// ────────────────────────────────────────────────────────────────
	// ① ビルボード行列（粒ごとに変わらないので1回だけ作る）
	//     ビュー行列はカメラのワールド行列の逆行列なので、逆行列を取れば戻る
	// ────────────────────────────────────────────────────────────────
	Matrix4x4 billboardMatrix = Inverse(camera.viewMatrix);

	// 平行移動の成分（4行目）を消して、回転だけ残す
	billboardMatrix.m[3][0] = 0.0f;
	billboardMatrix.m[3][1] = 0.0f;
	billboardMatrix.m[3][2] = 0.0f;

	// ────────────────────────────────────────────────────────────────
	// ② 生きている粒を、GPU 側の配列の先頭から詰めて書く
	//     erase しないので range-based for が使える
	// ────────────────────────────────────────────────────────────────
	uint32_t drawCount = 0;

	for (const Particle& particle : particles_) {

		// 枠が埋まったら、それ以上は書けない
		if (drawCount >= maxInstanceCount_) {
			break;
		}

		// 回転の作り方だけが違う
		Matrix4x4 worldMatrix{};

		if (useBillboard_) {
			worldMatrix =
				MakeScaleMatrix(particle.transform.scale)
				* billboardMatrix
				* MakeTranslateMatrix(particle.transform.translate);
		} else {
			worldMatrix = MakeAffineMatrix(
				particle.transform.scale,
				particle.transform.rotate,
				particle.transform.translate);
		}

		instancingData_[drawCount].WVP = worldMatrix * camera.viewProjectionMatrix;
		instancingData_[drawCount].World = worldMatrix;

		// 寿命によるフェード
		//   Update で死んだ粒は消えているので、ここでは必ず lifeTime > 0
		const float lifeRatio = particle.currentTime / particle.lifeTime;
		const float alpha = 1.0f - lifeRatio;

		instancingData_[drawCount].color = particle.color;
		instancingData_[drawCount].color.w = particle.color.w * alpha;

		++drawCount;
	}

	drawCount_ = drawCount;

	// ────────────────────────────────────────────────────────────────
	// ③ 1個も無いなら、コマンドは積まない
	// ────────────────────────────────────────────────────────────────
	if (drawCount_ == 0) {
		return;
	}

	ID3D12GraphicsCommandList* commandList = context_.dxCommon->GetCommandList().Get();

	// パイプライン（RootSignature + PSO + トポロジ）
	context_.particlePipeline->SetPipeline(blendMode_);

	// 頂点バッファ（板1枚ぶん）
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// マテリアル（PS b0）
	commandList->SetGraphicsRootConstantBufferView(
		ParticlePipeline::kRootParameterMaterial,
		materialResource_->GetGPUVirtualAddress());

	// 行列の配列（VS t0）… ここが Instancing の肝
	commandList->SetGraphicsRootDescriptorTable(
		ParticlePipeline::kRootParameterInstancing,
		context_.dxCommon->GetSrvGpuDescriptorHandle(instancingSrvIndex_));

	// テクスチャ（PS t0）
	commandList->SetGraphicsRootDescriptorTable(
		ParticlePipeline::kRootParameterTexture,
		context_.textureManager->GetSrvGpuHandle(textureSrvIndex_));

	// 描画！ 6頂点の板を drawCount_ 個ぶん、1回の命令で描く
	commandList->DrawInstanced(vertexCount_, drawCount_, 0, 0);
}
// ════════════════════════════════════════════════════════════════════
// 粒を1個追加する
//   最大数を超えても追加はできる。描かれるのが最大数までというだけ
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::Add(const Particle& particle) {
	particles_.push_back(particle);
}

// ════════════════════════════════════════════════════════════════════
// 全部消す
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::Clear() {
	particles_.clear();
	drawCount_ = 0;   // 次の Update まで Draw されても困らないように
}

// ════════════════════════════════════════════════════════════════════
// 場を追加する
//   unique_ptr を値で受け取る＝所有権をもらう
// ════════════════════════════════════════════════════════════════════
void ParticleSystem::AddField(std::unique_ptr<Field> field) {
	fields_.push_back(std::move(field));
}