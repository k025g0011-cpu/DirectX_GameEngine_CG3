#pragma once

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

#include "CameraData.h"
#include "EngineContext.h"
#include "MathUtility.h"
#include "Object3dStructs.h"   // DirectionalLight
#include "Camera.h"
#include "Editor.h"
#include "GameObject.h"
#include "Sprite.h"

// 前方宣言
class Model;

// ════════════════════════════════════════════════════════════════════
// ゲームのシーン
//   このゲームで「何を出して、どう動かして、どう描くか」を持つ
//   エンジンの道具は EngineContext 経由で借りる
//   エディタの画面構成は Editor に任せる
// ════════════════════════════════════════════════════════════════════
class GameScene {
public:
	/// <summary>初期化。モデルの読み込みとオブジェクトの配置</summary>
	void Initialize(const EngineContext& context);

	/// <summary>毎フレームの更新（描画コマンドは積まない）</summary>
	void Update();

	/// <summary>オフスクリーンへの描画</summary>
	void Draw();

private:
	/// <summary>GameObject を作ってシーンに登録し、そのポインタを返す</summary>
	GameObject* CreateGameObject(const std::string& name);

	/// <summary>シーンの中身を描く。描画先は呼び出し側が設定済み</summary>
	void DrawScene(const CameraData& camera);

	/// <summary>基準グリッドを PrimitiveDrawer に予約する</summary>
	void DrawGrid();

	/// <summary>シーン共通の定数（ライト・カメラ）をルートに設定する</summary>
	///   ルートシグネチャを設定し直すと引数が無効になるので、そのたびに呼ぶ
	void SetSceneConstants(const CameraData& camera);

	/// <summary>シーン内のライトを集めて定数バッファに詰める</summary>
	void CollectLights();

	/// <summary>全オブジェクトのデバッグ線を PrimitiveDrawer に予約する</summary>
	void DrawGizmos();

	/// <summary>設定パネルの中身（このゲーム固有）</summary>
	void DrawSettingsPanel();

	/// <summary>デバッグパネルの中身（このゲーム固有）</summary>
	void DrawDebugPanel();

	// ────────────────────────────────────────────────────────────────
	// エンジンから借りている道具
	// ────────────────────────────────────────────────────────────────
	EngineContext context_{};

	// ────────────────────────────────────────────────────────────────
	// エディタ（画面構成・編集カメラ・描画先を持つ）
	// ────────────────────────────────────────────────────────────────
	Editor editor_;

	// ────────────────────────────────────────────────────────────────
	// ゲームカメラ
	// ────────────────────────────────────────────────────────────────
	Camera camera_;

	// ────────────────────────────────────────────────────────────────
	// モデル（ModelManager が所有。ここは借りているだけ）
	// ────────────────────────────────────────────────────────────────
	Model* sphereModel_ = nullptr;
	Model* suzanneModel_ = nullptr;

	// ────────────────────────────────────────────────────────────────
	// シーンに置かれたオブジェクト（こちらが所有する）
	// ────────────────────────────────────────────────────────────────
	std::vector<std::unique_ptr<GameObject>> gameObjects_;

	GameObject* sphereObject_ = nullptr;
	GameObject* suzanneObject_ = nullptr;
	GameObject* particleObject_ = nullptr;
	// ────────────────────────────────────────────────────────────────
	// スプライト（まだコンポーネント化していない）
	// ────────────────────────────────────────────────────────────────
	Sprite sprite_;

	// ────────────────────────────────────────────────────────────────
	// カメラの位置（鏡面反射用。シーンに1つ）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	CameraForGPU* cameraData_ = nullptr;


	// ────────────────────────────────────────────────────────────────
	// ライトの個数（毎フレーム CollectLights が書き換える）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> lightCountResource_;
	LightCount* lightCountData_ = nullptr;

	// ────────────────────────────────────────────────────────────────
	// 平行光源（シーンに1つ）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;

	// ────────────────────────────────────────────────────────────────
	// 点光源
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
	PointLight* pointLightData_ = nullptr;

	// ────────────────────────────────────────────────────────────────
	// スポットライト
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
	SpotLight* spotLightData_ = nullptr;
	// ────────────────────────────────────────────────────────────────
	// ImGui で操作する値
	// ────────────────────────────────────────────────────────────────
	Transform uvTransformModel_{
		{ 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	Transform uvTransformSprite_{
		{ 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

	// ────────────────────────────────────────────────────────────────
	// グリッドの設定
	// ────────────────────────────────────────────────────────────────
	static constexpr int32_t kGridHalfCount = 10;    // 中心から何マス伸ばすか
	static constexpr float   kGridSpacing = 1.0f;    // 1マスの大きさ
};