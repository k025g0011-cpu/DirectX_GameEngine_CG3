#pragma once

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

#include "EngineContext.h"
#include "MathUtility.h"
#include "Object3dStructs.h"   // DirectionalLight
#include "Camera.h"
#include "DebugCamera.h"
#include "GameObject.h"
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
	/// <summary>GameObject を作ってシーンに登録し、そのポインタを返す</summary>
	/// <param name="name">ヒエラルキーに出る名前</param>
	GameObject* CreateGameObject(const std::string& name);

	/// <summary>ImGui のウィンドウを構築する</summary>
	void UpdateImGui();

	/// <summary>ヒエラルキーとインスペクタ（エディタの原型）</summary>
	void DrawHierarchyAndInspector();

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

	// ────────────────────────────────────────────────────────────────
	// シーンに置かれたオブジェクト（こちらが所有する）
	//   unique_ptr なので、vector が再確保されても中身の住所は動かない
	// ────────────────────────────────────────────────────────────────
	std::vector<std::unique_ptr<GameObject>> gameObjects_;

	// よく触るものは、毎回探さずポインタで覚えておく
	GameObject* sphereObject_ = nullptr;
	GameObject* suzanneObject_ = nullptr;

	// ヒエラルキーで選択中のオブジェクト（-1 = 未選択）
	int32_t selectedObjectIndex_ = -1;

	// ────────────────────────────────────────────────────────────────
	// スプライト（まだコンポーネント化していない）
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