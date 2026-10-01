#include "ModelInstance.h"

#include <cassert>

#include "DirectXCommon.h"
#include "Model.h"

// ════════════════════════════════════════════════════════════════════
// 初期化
//   1体につき必要なのは「座標変換の定数バッファ」だけ
// ════════════════════════════════════════════════════════════════════
void ModelInstance::Initialize(DirectXCommon* dxCommon, Model* model) {

	assert(dxCommon != nullptr);

	dxCommon_ = dxCommon;
	model_ = model;

	transformationMatrixResource_ = dxCommon_->CreateBufferResource(sizeof(TransformationMatrix));
	transformationMatrixResource_->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixData_));
	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();

	worldMatrix_ = MakeIdentity4x4();
}

// ════════════════════════════════════════════════════════════════════
// 描画
// ════════════════════════════════════════════════════════════════════
void ModelInstance::Draw(const Matrix4x4& viewProjectionMatrix) {

	// 非表示、またはモデルが割り当てられていなければ何もしない
	if (!isVisible_ || model_ == nullptr) {
		return;
	}

	// ────────────────────────────────────────
	// ① この1体のワールド行列を作る
	// ────────────────────────────────────────
	worldMatrix_ = MakeAffineMatrix(
		transform_.scale, transform_.rotate, transform_.translate);

	// ────────────────────────────────────────
	// ② 自分の定数バッファに書き込む
	//     ここが1体ごとに別なので、複数体を別の位置に描ける
	// ────────────────────────────────────────
	transformationMatrixData_->WVP = worldMatrix_ * viewProjectionMatrix;
	transformationMatrixData_->World = worldMatrix_;

	// ────────────────────────────────────────
	// ③ 自分の定数バッファのアドレスを渡して、Model に描いてもらう
	// ────────────────────────────────────────
	model_->Draw(transformationMatrixResource_->GetGPUVirtualAddress());
}