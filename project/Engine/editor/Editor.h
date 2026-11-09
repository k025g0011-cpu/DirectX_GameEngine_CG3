#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CameraData.h"
#include "DebugCamera.h"
#include "EditorLayout.h"
#include "EngineContext.h"
#include "RenderTexture.h"

class Camera;
class GameObject;

/// <summary>ビューに何を映すか</summary>
enum class ViewMode : uint32_t {
	Edit,   // デバッグカメラ視点
	Game,   // ゲームカメラ視点
};

/// <summary>エディタ画面（レイアウト・ツールバー・ビュー・アセット）を持つ係</summary>
class Editor {
public:
	void Initialize(const EngineContext& context, Camera* gameCamera);

	/// <summary>毎フレーム。レイアウト計算 → ドロップ取り込み → 各パネル描画 → デバッグカメラ更新</summary>
	void Update();

	/// <summary>オブジェクト一覧と詳細パネルを描く（GameScene から所有物を借りる）</summary>
	void DrawObjectPanels(const std::vector<std::unique_ptr<GameObject>>& gameObjects);

	/// <summary>基準グリッドと座標軸を PrimitiveDrawer に積む</summary>
	///   エディタの備品なので、シーンではなくここが持つ
	///   表示するかどうかの判定も中で行う（フラグを持っているのがこのクラスのため）
	///   線を溜めるだけなので、Reset と Draw の間ならどこで呼んでもよい
	void DrawEditorGizmos();

	/// <summary>ゲーム固有パネルにレイアウトの枠を貸す</summary>
	bool BeginPanel(EditorPanel panel) { return layout_.BeginPanel(panel); }
	void EndPanel() { layout_.EndPanel(); }

	/// <summary>いま描画先にすべきレンダーテクスチャ</summary>
	RenderTexture& GetActiveRenderTexture();

	/// <summary>いま使うべきビュープロジェクション行列</summary>
	const Matrix4x4& GetViewProjectionMatrix() const;

	/// <summary>いま使うべきビュー行列（カメラの向きが要るとき用）</summary>
	const Matrix4x4& GetViewMatrix() const;

	/// <summary>いま使うべき射影行列</summary>
	const Matrix4x4& GetProjectionMatrix() const;

	/// <summary>今のカメラの行列一式。描画時はこれを配る</summary>
	CameraData GetCameraData() const;

	ViewMode GetViewMode() const { return viewMode_; }
	bool IsViewHovered() const { return isViewHovered_; }
	const DebugCamera& GetDebugCamera() const { return debugCamera_; }

	/// <summary>オブジェクトのデバッグ線を描くか</summary>
	///   線を積むのはシーン側なので、判定だけ外へ貸す
	bool IsGizmosVisible() const { return showGizmos_; }

	/// <summary>アセットパネルで選択中のアセット（未選択なら空文字）</summary>
	const std::string& GetSelectedAssetPath() const { return selectedAssetPath_; }

private:
	void ImportDroppedFiles();                        
	void DrawToolbar();
	void DrawViewPanel();
	bool DrawViewImage(RenderTexture& renderTexture);
	void DrawAssetPanel();
	/// <summary>1オブジェクトとその子を再帰で描く</summary>
	void DrawObjectNode(GameObject* object);

	const EngineContext* context_ = nullptr;
	Camera* gameCamera_ = nullptr;

	EditorLayout layout_;
	DebugCamera debugCamera_;

	ViewMode viewMode_ = ViewMode::Edit;
	RenderTexture editRenderTexture_;
	RenderTexture gameRenderTexture_;

	bool isViewHovered_ = false;

	// ────────────────────────────────────────────────────────────────
	// 表示の切り替え（ツールバーから操作する）
	//   エフェクトや見た目を確認するとき、線が全部邪魔になる
	// ────────────────────────────────────────────────────────────────
	bool showGrid_ = true;      // 基準グリッドと座標軸
	bool showGizmos_ = true;    // 各オブジェクトのデバッグ線

	// いま選んでいるオブジェクト（借りているだけ。所有権は Scene にある）
	GameObject* selectedObject_ = nullptr;

	// ドラッグで親子を変える予約
	//   走査中に SetParent すると children_ が書き換わって壊れるので、
	//   描き終わってから実行する（pendingChild_ が nullptr でなければ予約あり）
	GameObject* pendingChild_ = nullptr;
	GameObject* pendingParent_ = nullptr;

	std::string selectedAssetPath_;   // 選択中アセット。添字ではなくパスで持つ
	int32_t assetTypeFilter_ = 0;     // 0=すべて / 1=Model / 2=Texture / 3=Scene / 4=Audio

	std::string lastImportMessage_;   // 直近のドロップ結果（アセットパネルに表示）
};