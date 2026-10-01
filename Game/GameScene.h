#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "EngineContext.h"
#include "MathUtility.h"
#include "Object3dStructs.h"   // DirectionalLight
#include "Camera.h"
#include "DebugCamera.h"
#include "ModelInstance.h"
#include "Sprite.h"

// 前方宣言
class Model;

// ════════════════════════════════════════════════════════════════════
// ゲームのシーン
//   このゲームで「何を出して、どう動かして、どう描くか」を持つ
//   エンジンの道具は EngineContext 経由で借りる
// ════════════════════════════════════════════════════════════════════
class GameScene {
public:
	/// <summary>初期化。モデルの読み込みとオブジェクトの配置</summary>
	void Initialize(const EngineContext& context);

	/// <summary>毎フレームの更新（描画コマンドは積まない）</summary>
	void Update();

	/// <summary>描画コマンドを積む。PreDraw と PostDraw の間で呼ぶ</summary>
	void Draw();

	/// <summary>ゲームビューが操作対象か（エディタから毎フレーム伝えられる）</summary>
	void SetGameViewActive(bool active) { isGameViewActive_ = active; }

private:
	/// <summary>ImGui のウィンドウを構築する</summary>
	void UpdateImGui();

	/// <summary>基準グリッドを PrimitiveDrawer に予約する</summary>
	void DrawGrid();

	/// <summary>いま使うカメラのビュー×射影行列を返す</summary>
	const Matrix4x4& GetViewProjectionMatrix() const;

	// ────────────────────────────────────────────────────────────────
	// エンジンから借りている道具
	// ────────────────────────────────────────────────────────────────
	EngineContext context_{};

	// ────────────────────────────────────────────────────────────────
	// カメラ
	// ────────────────────────────────────────────────────────────────
	Camera      camera_;
	DebugCamera debugCamera_;
	bool        useDebugCamera_ = false;   // F1 で切り替え

	// ────────────────────────────────────────────────────────────────
	// モデル（ModelManager が所有。ここは借りているだけ）
	// ────────────────────────────────────────────────────────────────
	Model* sphereModel_ = nullptr;
	Model* suzanneModel_ = nullptr;

	// 画面に出す実体
	ModelInstance sphereInstance_;
	ModelInstance suzanneInstance_;

	// ────────────────────────────────────────────────────────────────
	// スプライト
	// ────────────────────────────────────────────────────────────────
	Sprite sprite_;

	// ────────────────────────────────────────────────────────────────
	// 平行光源（シーンに1つ）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;

	// ────────────────────────────────────────────────────────────────
	// ImGui で操作する値
	// ────────────────────────────────────────────────────────────────
	Transform uvTransformModel_{
		{ 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	Transform uvTransformSprite_{
		{ 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

	// グリッドの設定
	static constexpr int32_t kGridHalfCount = 10;    // 中心から何マス伸ばすか
	static constexpr float   kGridSpacing = 1.0f;    // 1マスの大きさ

	// ゲームビューが操作対象か（エディタから毎フレーム伝えられる）
	bool isGameViewActive_ = false;
};