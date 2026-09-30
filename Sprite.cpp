#include "Sprite.h"

#include <cassert>

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "Object3dPipeline.h"
#include "WinApp.h"

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void Sprite::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
	uint32_t textureSrvIndex, const Vector2& size) {

	assert(dxCommon != nullptr);
	assert(textureManager != nullptr);

	dxCommon_ = dxCommon;
	textureManager_ = textureManager;
	textureSrvIndex_ = textureSrvIndex;
	size_ = size;

	// ════════════════════════════════════════
	// 頂点バッファ（4頂点）
	//   中身は Draw のたびに書き直すので、ここでは箱だけ作る
	// ════════════════════════════════════════
	vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * 4);

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// Map したまま持ち続ける（毎フレーム書き換えるので Unmap しない）
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

	// ════════════════════════════════════════
	// インデックスバッファ（6個 ＝ 三角形2枚）
	//   頂点の並びは変わらないので、ここで1回書いたら以降触らない
	//
	//     v1 ─── v3      左上 ─── 右上
	//      │ ＼   │
	//     v0 ─── v2      左下 ─── 右下
	// ════════════════════════════════════════
	indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 6);

	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;   // インデックス1つが32bit整数

	uint32_t* indexData = nullptr;
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
	indexData[0] = 0;  indexData[1] = 1;  indexData[2] = 2;   // 1枚目：左下→左上→右下
	indexData[3] = 1;  indexData[4] = 3;  indexData[5] = 2;   // 2枚目：左上→右上→右下

	// ════════════════════════════════════════
	// 座標変換の定数バッファ（VS b0）
	// ════════════════════════════════════════
	transformationMatrixResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
	transformationMatrixResource_->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixData_));
	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();

	// ════════════════════════════════════════
	// マテリアルの定数バッファ（PS b0）
	//   スプライトはライティングしない（画面に貼る絵なので法線に意味がない）
	// ════════════════════════════════════════
	materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	materialData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };   // 白（＝テクスチャそのまま）
	materialData_->enableLighting = false;
	materialData_->uvTransform = MakeIdentity4x4();

	// ════════════════════════════════════════
	// 平行投影行列
	//   画面の左上を (0,0)、右下を (1280,720) とするピクセル座標系を作る
	//   画面サイズが変わらないので1回だけ作る
	// ════════════════════════════════════════
	projectionMatrix_ = MakeOrthographicMatrix(
		0.0f, 0.0f,                                 // 左, 上
		static_cast<float>(WinApp::kClientWidth),   // 右
		static_cast<float>(WinApp::kClientHeight),  // 下
		0.0f, 100.0f);                              // near, far
}

// ════════════════════════════════════════════════════════════════════
// 4頂点を作り直す
//   size_ と anchorPoint_ から、四角形の四隅の座標を求める
// ════════════════════════════════════════════════════════════════════
void Sprite::UpdateVertices() {

	// ────────────────────────────────────────
	// アンカーポイントを原点として、四隅までの距離を出す
	//
	//   anchorPoint (0,0)          anchorPoint (0.5,0.5)
	//   ●───────┐                 ┌───────┐
	//   │       │                 │   ●   │
	//   └───────┘                 └───────┘
	//   left=0, right=size        left=-size/2, right=+size/2
	// ────────────────────────────────────────
	const float left = -anchorPoint_.x * size_.x;
	const float right = (1.0f - anchorPoint_.x) * size_.x;
	const float top = -anchorPoint_.y * size_.y;
	const float bottom = (1.0f - anchorPoint_.y) * size_.y;

	// 画面はY下向きなので、bottom のほうが top より値が大きい

	// 法線は画面手前向き。ライティングしないので実際には使われない
	const Vector3 kNormal = { 0.0f, 0.0f, -1.0f };

	vertexData_[0].position = { left,  bottom, 0.0f };   // 左下
	vertexData_[0].texcoord = { 0.0f, 1.0f };
	vertexData_[0].normal = kNormal;

	vertexData_[1].position = { left,  top,    0.0f };   // 左上
	vertexData_[1].texcoord = { 0.0f, 0.0f };
	vertexData_[1].normal = kNormal;

	vertexData_[2].position = { right, bottom, 0.0f };   // 右下
	vertexData_[2].texcoord = { 1.0f, 1.0f };
	vertexData_[2].normal = kNormal;

	vertexData_[3].position = { right, top,    0.0f };   // 右上
	vertexData_[3].texcoord = { 1.0f, 0.0f };
	vertexData_[3].normal = kNormal;
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   RootSignature / PSO / トポロジは呼び出し側が設定済みであること
// ════════════════════════════════════════════════════════════════════
void Sprite::Draw() {

	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

	// ────────────────────────────────────────
	// ① 頂点を作り直す（大きさ・アンカーポイントの変更を反映）
	// ────────────────────────────────────────
	UpdateVertices();

	// ────────────────────────────────────────
	// ② 行列を更新する
	//     2Dなのでカメラ（ビュー行列）は無い ＝ 単位行列
	//     よって WVP = World × Projection
	// ────────────────────────────────────────
	Matrix4x4 worldMatrix = MakeAffineMatrix(
		{ 1.0f, 1.0f, 1.0f },                      // 拡縮は頂点側でやるので単位
		{ 0.0f, 0.0f, rotation_ },                 // 画面に垂直な軸（Z）まわりの回転
		{ position_.x, position_.y, 0.0f });       // 画面上の位置

	transformationMatrixData_->WVP = worldMatrix * projectionMatrix_;
	transformationMatrixData_->World = worldMatrix;

	// ────────────────────────────────────────
	// ③ 描画コマンドを積む
	// ────────────────────────────────────────
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterMaterial,
		materialResource_->GetGPUVirtualAddress());  // シェーダーにルートパラメータ[0]を介して、Sprite のmaterialResourceアドレスを渡す

	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterTransform,
		transformationMatrixResource_->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[1]を介して、Sprite のtransformationMatrixResource_アドレスを渡す

	commandList->SetGraphicsRootDescriptorTable(
		Object3dPipeline::kRootParameterTexture,
		textureManager_->GetSrvGpuHandle(textureSrvIndex_));  // シェーダーにルートパラメータ[2]を介して、Srv上の使いたいテクスチャのアドレスを渡す（textureSrvIndex_でテクスチャ指定）

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);

	// 6個のインデックスで三角形2枚＝四角形1枚を描く
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}