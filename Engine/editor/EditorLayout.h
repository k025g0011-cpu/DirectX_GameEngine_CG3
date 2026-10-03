#pragma once

#include <cstdint>

// ════════════════════════════════════════════════════════════════════
// エディタのパネル
//   kMaxCount を最後に置くと、配列のサイズが自動で決まる
// ════════════════════════════════════════════════════════════════════
/// <summary>エディタのパネル種別</summary>
enum class EditorPanel : uint32_t {
	Toolbar,    // 行1       : 再生／一時停止、Edit / Game 切り替え
	View,       // 行2 中央   : ゲーム画面（オフスクリーンの RenderTexture）
	Hierarchy,  // 行2 左     : オブジェクト一覧
	Inspector,  // 行2 右     : 選択中オブジェクトの詳細
	Asset,      // 行3 左     : アセット一覧           
	Debug,      // 行3 中央   : ログ・FPS など
	Settings,   // 行3 右     : ライトや UV などゲーム固有の設定
	kMaxCount,  // 種別の数（配列サイズ用。これより下に増やさない）
};

// ════════════════════════════════════════════════════════════════════
// エディタの画面レイアウト
//   各パネルの位置とサイズを毎フレーム計算し、固定配置で開く
//   ・ドッキングは使わない（並べ替えない前提）
//   ・ウィンドウのリサイズには自動で追従する
// ════════════════════════════════════════════════════════════════════
class EditorLayout {
public:
	/// <summary>画面サイズから全パネルの矩形を計算する。毎フレーム最初に呼ぶ</summary>
	void Update();

	/// <summary>パネルを開く。ImGui::Begin と同じく、戻り値に関わらず EndPanel を呼ぶこと</summary>
	bool BeginPanel(EditorPanel panel);

	/// <summary>パネルを閉じる</summary>
	void EndPanel();

	/// <summary>指定した点がパネルの矩形の中にあるか</summary>
	/// <param name="panel">調べたいパネル</param>
	/// <param name="x">クライアント座標 X（= ImGui の画面座標）</param>
	/// <param name="y">クライアント座標 Y</param>
	bool IsInsidePanel(EditorPanel panel, float x, float y) const;

private:
	// 1つのパネルの矩形（imgui.h に依存しないよう自前で持つ）
	struct Rect {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	Rect rects_[static_cast<uint32_t>(EditorPanel::kMaxCount)]{};

	// ────────────────────────────────────────────────────────────────
	// レイアウトの寸法
	// ────────────────────────────────────────────────────────────────
	// ツールバーの高さは ImGui のスタイル（フォントサイズ）から毎フレーム求める
	//   固定値にすると、フォントを変えたときに中身がはみ出す
	float toolbarHeight_ = 40.0f;

	static constexpr float kHierarchyWidth = 180.0f;   // 260 → 220
	static constexpr float kInspectorWidth = 280.0f;   // 320 → 300
	static constexpr float kBottomHeight = 180.0f;
	static constexpr float kAssetWidth = 360.0f;  
	static constexpr float kSettingsWidth = 320.0f;   
};