#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

	/// <summary>ゲーム固有パネルにレイアウトの枠を貸す</summary>
	bool BeginPanel(EditorPanel panel) { return layout_.BeginPanel(panel); }
	void EndPanel() { layout_.EndPanel(); }

	/// <summary>いま描画先にすべきレンダーテクスチャ</summary>
	RenderTexture& GetActiveRenderTexture();

	/// <summary>いま使うべきビュープロジェクション行列</summary>
	const Matrix4x4& GetViewProjectionMatrix() const;

	ViewMode GetViewMode() const { return viewMode_; }
	bool IsViewHovered() const { return isViewHovered_; }
	const DebugCamera& GetDebugCamera() const { return debugCamera_; }

	/// <summary>アセットパネルで選択中のアセット（未選択なら空文字）</summary>
	const std::string& GetSelectedAssetPath() const { return selectedAssetPath_; }

private:
	void ImportDroppedFiles();                        
	void DrawToolbar();
	void DrawViewPanel();
	bool DrawViewImage(RenderTexture& renderTexture);
	void DrawAssetPanel();

	const EngineContext* context_ = nullptr;
	Camera* gameCamera_ = nullptr;

	EditorLayout layout_;
	DebugCamera debugCamera_;

	ViewMode viewMode_ = ViewMode::Edit;
	RenderTexture editRenderTexture_;
	RenderTexture gameRenderTexture_;

	bool isViewHovered_ = false;
	int32_t selectedObjectIndex_ = -1;

	std::string selectedAssetPath_;   // 選択中アセット。添字ではなくパスで持つ
	int32_t assetTypeFilter_ = 0;     // 0=すべて / 1=Model / 2=Texture / 3=Scene / 4=Audio

	std::string lastImportMessage_;   // 直近のドロップ結果（アセットパネルに表示）
};